param(
    [switch]$BuildHosted
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase4c-shell-object-registry"
$BackupRoot = Join-Path $TempRoot "restore-backup"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "appmodel-phase4c-shell-object-registry-$stamp.log"
$TempArtifact = Join-Path $TempRoot "phase4c-shell-object-registry-smoke.txt"

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

function Assert-NotContains {
    param(
        [object]$Text,
        [string]$Needle,
        [string]$Reason
    )

    $haystack = ($Text | Out-String)
    if ($haystack -match [regex]::Escape($Needle)) {
        throw "Unexpected text for ${Reason}: $Needle"
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

function Get-FileText {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return ""
    }
    return Get-Content -LiteralPath $Path -Raw
}

function Backup-TrackedArtifacts {
    $tracked = @(
        "desktop.json",
        "desktop.state",
        "display-options.cfg"
    )

    $state = @{}
    foreach ($relativePath in $tracked) {
        $absolutePath = Join-Path $Root $relativePath
        $backupPath = Join-Path $BackupRoot $relativePath
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backupPath) | Out-Null

        $exists = Test-Path -LiteralPath $absolutePath
        $state[$relativePath] = [pscustomobject]@{
            Exists = $exists
            Path = $absolutePath
            BackupPath = $backupPath
            BeforeText = if ($exists) { Get-FileText $absolutePath } else { "" }
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

$artifactState = Backup-TrackedArtifacts
$summaryBeforeOutput = $null
$shellOutput = $null
$computerFilesOutput = $null
$forceOffSummaryOutput = $null
$resetSummaryOutput = $null
$launchCompareOutput = $null
$cleanupOk = $false
$artifactRestoreOk = $false

try {
    if ($BuildHosted) {
        & cmd.exe /c build.bat
        if ($LASTEXITCODE -ne 0) {
            throw "Hosted build failed with exit code $LASTEXITCODE"
        }
    }

    $summaryBeforeOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.summary"
    )
    $shellOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.shell-objects"
    )
    $computerFilesOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.resolve ComputerFiles"
    )
    $forceOffSummaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.appmodel.summary"
    )
    $resetSummaryOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.summary"
    )
    $launchCompareOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch.compare"
    )

    Assert-Contains $summaryBeforeOutput "appModelPhase4CShellObjectRegistryExists=true" "shell registry exists marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CShellObjectIdsUnique=true" "shell registry unique ids marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CShellObjectDisplayNamesNonEmpty=true" "shell registry display names marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CShellAliasesResolve=true" "shell alias resolution marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CShellHandlersResolveToRegistry=true" "shell handler resolution marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CRightColumnShellObjectsRegistered=true" "right column marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CSystemObjectsRegistered=true" "system objects marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CTrashOpenOnlySafe=true" "trash safe open marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CTrashDestructiveActionsExcluded=true" "trash destructive exclusion marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CComputerFilesFallbackPreserved=true" "ComputerFiles fallback marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CRecentProgramsNotPolluted=true" "recent programs marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CVisibleLaunchBehaviorChanged=false" "visible behavior marker"
    Assert-Contains $summaryBeforeOutput "appModelPhase4CPersistentDesktopStorageWrites=false" "persistent storage marker"
    Assert-Contains $summaryBeforeOutput "appModelActiveDispatchEnabled=true" "product-default active typed dispatch"
    Assert-Contains $summaryBeforeOutput "appModelActiveDispatchEffectiveStateSource=product-default" "product-default typed dispatch source"

    Assert-Contains $shellOutput "[ShellObjectRegistryV1]" "shell object registry section"
    Assert-Contains $shellOutput "registryExists: true" "registry exists output"
    Assert-Contains $shellOutput "shellObjectRegistryIdsUnique: true" "registry ids unique output"
    Assert-Contains $shellOutput "shellObjectRegistryDisplayNamesNonEmpty: true" "registry display names output"
    Assert-Contains $shellOutput "shellObjectRegistryAliasesResolve: true" "registry aliases output"
    Assert-Contains $shellOutput "shellObjectRegistryHandlersResolveToRegistry: true" "registry handlers output"
    Assert-Contains $shellOutput "rightColumnShellObjectsRegistered: true" "registry right column output"
    Assert-Contains $shellOutput "systemObjectsRegistered: true" "registry system objects output"
    Assert-Contains $shellOutput "trashOpenOnlySafe: true" "registry trash open output"
    Assert-Contains $shellOutput "trashDestructiveActionsExcluded: true" "registry trash destructive output"
    Assert-Contains $shellOutput "computerFilesFallbackPreserved: true" "registry ComputerFiles output"
    Assert-Contains $shellOutput "recentProgramsNotPolluted: true" "registry recent programs output"
    Assert-Contains $shellOutput "record id=gxos.shell.this-system displayName=This System" "This System registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.files displayName=Files" "Files registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.documents displayName=Documents" "Documents registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.pictures displayName=Pictures" "Pictures registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.music displayName=Music" "Music registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.network displayName=Network" "Network registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.settings displayName=Settings" "Settings registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.control-panel displayName=Control Panel" "Control Panel registry record"
    Assert-Contains $shellOutput "record id=gxos.shell.trash-open displayName=Trash" "Trash registry record"
    Assert-Contains $shellOutput "alias=This System resolved=true shellObjectId=gxos.shell.this-system" "This System alias resolution"
    Assert-Contains $shellOutput "alias=Computer resolved=true shellObjectId=gxos.shell.this-system" "Computer alias resolution"
    Assert-Contains $shellOutput "alias=Files resolved=true shellObjectId=gxos.shell.files" "Files alias resolution"
    Assert-Contains $shellOutput "alias=File Manager resolved=true shellObjectId=gxos.shell.files" "File Manager alias resolution"
    Assert-Contains $shellOutput "alias=File Explorer resolved=true shellObjectId=gxos.shell.files" "File Explorer alias resolution"
    Assert-Contains $shellOutput "alias=Settings resolved=true shellObjectId=gxos.shell.settings" "Settings alias resolution"
    Assert-Contains $shellOutput "alias=System Settings resolved=true shellObjectId=gxos.shell.settings" "System Settings alias resolution"
    Assert-Contains $shellOutput "alias=Display Options resolved=true shellObjectId=gxos.shell.settings" "Display Options alias resolution"
    Assert-Contains $shellOutput "alias=Control Panel resolved=true shellObjectId=gxos.shell.control-panel" "Control Panel alias resolution"
    Assert-Contains $shellOutput "alias=Trash resolved=true shellObjectId=gxos.shell.trash-open" "Trash alias resolution"
    Assert-Contains $shellOutput "handlerResolved=true" "registry handler resolution fields"
    Assert-Contains $shellOutput "canonicalLaunchTargetName=FileExplorer" "Files canonical launch target"
    Assert-Contains $shellOutput "canonicalLaunchTargetName=DisplayOptions" "Settings canonical launch target"
    Assert-Contains $shellOutput "canonicalLaunchTargetName=ControlPanel" "Control Panel canonical launch target"
    Assert-Contains $shellOutput "canonicalLaunchTargetName=Trash" "Trash canonical launch target"
    Assert-Contains $shellOutput "targetKind=filesystem-folder" "filesystem folder target kind"
    Assert-Contains $shellOutput "targetKind=app/system-panel" "app/system panel target kind"
    Assert-Contains $shellOutput "targetKind=virtual-object" "virtual object target kind"
    Assert-Contains $shellOutput "activeTypedDispatchMayOwn=true" "active dispatch ownership field"
    Assert-Contains $shellOutput "shouldWriteRecentPrograms=true" "recent programs field"
    Assert-NotContains $shellOutput "alias=Empty Trash" "destructive trash alias absence"
    Assert-NotContains $shellOutput "alias=delete" "destructive trash action absence"
    Assert-NotContains $shellOutput "alias=restore" "destructive trash action absence"
    Assert-NotContains $shellOutput "alias=purge" "destructive trash action absence"
    Assert-RegexCountAtLeast $shellOutput '^  record id=gxos\.shell\.' 10 "registry record count"

    Assert-Contains $computerFilesOutput "resolvedType: ShellAction" "ComputerFiles resolved type"
    Assert-Contains $computerFilesOutput "dispatchLaunchName: FileExplorer" "ComputerFiles fallback dispatch"
    Assert-Contains $computerFilesOutput "shellAction: ComputerFiles" "ComputerFiles shell action label"
    Assert-Contains $computerFilesOutput "status: resolved-shell" "ComputerFiles resolved status"

    Assert-Contains $forceOffSummaryOutput "appModelActiveDispatchEnabled=false" "force-off gate state"
    Assert-Contains $forceOffSummaryOutput "appModelActiveDispatchEffectiveStateSource=force-off" "force-off state source"
    Assert-Contains $forceOffSummaryOutput "appModelActiveDispatchVisibleLaunchBehaviorChanged=false" "force-off visible behavior marker"
    Assert-Contains $forceOffSummaryOutput "appModelActiveDispatchPersistentDesktopStorageWrites=false" "force-off storage marker"
    Assert-Contains $forceOffSummaryOutput "appModelPhase4CShellObjectRegistryExists=true" "summary still reports shell registry"

    Assert-Contains $resetSummaryOutput "appModelActiveDispatchEnabled=true" "reset gate state"
    Assert-Contains $resetSummaryOutput "appModelActiveDispatchEffectiveStateSource=product-default" "reset source"
    Assert-Contains $resetSummaryOutput "appModelPhase4CShellObjectRegistryExists=true" "reset summary shell registry marker"
    Assert-Contains $resetSummaryOutput "appModelPhase4CVisibleLaunchBehaviorChanged=false" "reset visible behavior marker"
    Assert-Contains $resetSummaryOutput "appModelPhase4CPersistentDesktopStorageWrites=false" "reset storage marker"

    Assert-Contains $launchCompareOutput "overall: OK" "launch comparison health"
    Assert-Contains $launchCompareOutput "unexpectedDrift: 0" "launch comparison drift"

    $artifactRestoreOk = $true
    foreach ($entry in $artifactState.GetEnumerator()) {
        $record = $entry.Value
        if (-not $record.Exists) {
            if (Test-Path -LiteralPath $record.Path) {
                $artifactRestoreOk = $false
            }
            continue
        }

        $afterText = Get-FileText $record.Path
        if ($afterText -ne $record.BeforeText) {
            $artifactRestoreOk = $false
        }
    }

    $reportLines = @(
        "[AppModelPhase4CShellObjectRegistrySmoke]",
        "mode=diagnostic-only",
        "appModelPhase4CShellObjectRegistryExists=true",
        "appModelPhase4CShellObjectIdsUnique=true",
        "appModelPhase4CShellObjectDisplayNamesNonEmpty=true",
        "appModelPhase4CShellAliasesResolve=true",
        "appModelPhase4CShellHandlersResolveToRegistry=true",
        "appModelPhase4CRightColumnShellObjectsRegistered=true",
        "appModelPhase4CSystemObjectsRegistered=true",
        "appModelPhase4CTrashOpenOnlySafe=true",
        "appModelPhase4CTrashDestructiveActionsExcluded=true",
        "appModelPhase4CComputerFilesFallbackPreserved=true",
        "appModelPhase4CRecentProgramsNotPolluted=true",
        "appModelPhase4CVisibleLaunchBehaviorChanged=false",
        "appModelPhase4CPersistentDesktopStorageWrites=false",
        "appModelActiveDispatchEnabled=true",
        "appModelActiveDispatchEffectiveStateSource=product-default",
        "appModelPhase4CTemporarySmokeStateRestored=true",
        "launchCompare=OK",
        "result=PASS"
    )

    Set-Content -Path $TempArtifact -Value ($reportLines -join [Environment]::NewLine) -Encoding ASCII
} finally {
    Restore-TrackedArtifacts -State $artifactState
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
    $cleanupOk = -not (Test-Path -LiteralPath $TempRoot)
}

if (-not $cleanupOk) {
    throw "Temporary smoke artifacts were not cleaned up"
}

if (-not $artifactRestoreOk) {
    throw "Tracked smoke artifacts were not restored"
}

if ($null -eq $reportLines) {
    throw "Phase 4C registry smoke did not produce a report"
}

$finalReportLines = @($reportLines + @(
    "generatedSmokeArtifactsCleaned=$($cleanupOk.ToString().ToLowerInvariant())",
    "generatedSmokeArtifactsRestored=$($artifactRestoreOk.ToString().ToLowerInvariant())",
    "tempArtifact=$TempArtifact"
))
$report = $finalReportLines -join [Environment]::NewLine
Set-Content -Path $SmokeLog -Value ($report + [Environment]::NewLine) -Encoding ASCII

Write-Output $report
Write-Host "Smoke log: $SmokeLog"
