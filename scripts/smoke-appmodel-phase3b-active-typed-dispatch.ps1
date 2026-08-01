param(
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase3b-active-typed-dispatch"
$BackupRoot = Join-Path $TempRoot "restore-backup"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

if (-not (Test-Path $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$FixtureFolder = Join-Path $TempRoot "phase3b-safe-folder"
$FixtureText = Join-Path $TempRoot "phase3b-safe-open.txt"
$SmokeLog = Join-Path $LogDir "appmodel-phase3b-active-typed-dispatch-$stamp.log"

function Invoke-ServerCommands {
    param([string[]]$Commands)

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return (($inputText | & $Exe 2>&1) -join [Environment]::NewLine)
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Needle,
        [string]$Reason
    )

    if (-not $Text.Contains($Needle)) {
        throw "Missing expected text for ${Reason}: $Needle"
    }
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
        $parent = Split-Path -Parent $backupPath
        New-Item -ItemType Directory -Force -Path $parent | Out-Null

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

function Test-Case {
    param(
        [string]$Name,
        [bool]$Pass,
        [string]$Detail
    )

    [pscustomobject]@{
        Name = $Name
        Pass = $Pass
        Detail = $Detail
    }
}

$artifactState = Backup-TrackedArtifacts

try {
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null
    Set-Content -Path $FixtureText -Value @"
Phase 3B active typed dispatch smoke fixture
This file should open through Notepad when guarded active typed dispatch is enabled.
"@ -Encoding ASCII

    $output = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.appmodel.active-typed-dispatch-gate force-on",
        "desktop.launch Notepad",
        "desktop.launch FileExplorer",
        "desktop.launch Files",
        "desktop.launch Console",
        "desktop.launch Calculator",
        "desktop.launch Clock",
        "desktop.launch TaskManager",
        "desktop.launch DiskManager",
        "desktop.launch Paint",
        "desktop.launch Trash",
        "desktop.launch Control Panel",
        "desktop.launch Settings",
        "desktop.launch System Settings",
        "desktop.launch Computer",
        "desktop.launch This System",
        "desktop.launch Documents",
        "desktop.launch Pictures",
        "desktop.launch Music",
        "desktop.launch Network",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.launch AppModel",
        "desktop.launch TotallyUnknownLaunchThing",
        "desktop.appmodel.active-typed-dispatch-gate"
    )

    $defaultGateConfirmed =
    $output.Contains("command: desktop.appmodel.active-typed-dispatch-gate") -and
    $output.Contains("mode: status") -and
    $output.Contains("appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate") -and
    $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
    $output.Contains("appModelActiveDispatchEnabled=true") -and
    $output.Contains("appModelActiveDispatchRuntimePath=active") -and
    $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
    $output.Contains("runtimeLaunchBehaviorChanged=true") -and
    $output.Contains("visibleLaunchBehaviorChanged=false") -and
    $output.Contains("persistentDesktopStorageWrites=false") -and
    $output.Contains("appModelActiveDispatchToggleApplied=false")

$forceOffGateConfirmed =
    $output.Contains("mode: force-off") -and
    $output.Contains("appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate") -and
    $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
    $output.Contains("appModelActiveDispatchEnabled=false") -and
    $output.Contains("appModelActiveDispatchCurrentState=false") -and
    $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
    $output.Contains("runtimeLaunchBehaviorChanged=true") -and
    $output.Contains("visibleLaunchBehaviorChanged=false") -and
    $output.Contains("persistentDesktopStorageWrites=false") -and
    $output.Contains("appModelActiveDispatchToggleApplied=true")

$forceOnGateConfirmed =
    $output.Contains("mode: force-on") -and
    $output.Contains("appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate") -and
    $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
    $output.Contains("appModelActiveDispatchEnabled=true") -and
    $output.Contains("appModelActiveDispatchCurrentState=true") -and
    $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
    $output.Contains("runtimeLaunchBehaviorChanged=true") -and
    $output.Contains("visibleLaunchBehaviorChanged=false") -and
    $output.Contains("persistentDesktopStorageWrites=false") -and
    $output.Contains("appModelActiveDispatchToggleApplied=true")

$restoreGateConfirmed =
    $output.Contains("mode: reset") -and
    $output.Contains("appModelActiveDispatchEnabled=true") -and
    $output.Contains("appModelActiveDispatchCurrentState=true") -and
    $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
    $output.Contains("nonFatal=true")

    $offNotepadFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $offFolderOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $offTextOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $onNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $onFileExplorerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=FileExplorer classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $onFilesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Files classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $onConsoleConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Console classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Console") -and
        $output.Contains("reason=Active typed dispatch handled the Console launch")

    $onCalculatorConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Calculator classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Calculator") -and
        $output.Contains("reason=Active typed dispatch handled the Calculator launch")

    $onClockConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Clock classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Clock") -and
        $output.Contains("reason=Active typed dispatch handled the Clock launch")

    $onTaskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TaskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=TaskManager") -and
        $output.Contains("reason=Active typed dispatch handled the Task Manager launch")

    $onDiskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=DiskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DiskManager") -and
        $output.Contains("reason=Active typed dispatch handled the Disk Manager launch")

    $onPaintConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Paint classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Paint") -and
        $output.Contains("reason=Active typed dispatch handled the Paint launch")

    $onTrashConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Trash classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Trash") -and
        $output.Contains("reason=Active typed dispatch handled the Trash launch")

    $onControlPanelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Control Panel classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Control Panel") -and
        $output.Contains("reason=Active typed dispatch handled the Control Panel shell action")

    $onSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $onSystemSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=System Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $onComputerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Computer classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $onThisSystemConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=This System classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $onDocumentsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Documents classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $onPicturesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Pictures classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $onMusicConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Music classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $onNetworkConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Network classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $onFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $onTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $fallbackAppModelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=AppModel classification=LegacyAlias") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("selectedHandler=App Model Demo")

    $fallbackUnknownConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TotallyUnknownLaunchThing classification=Unknown") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("selectedHandler=TotallyUnknownLaunchThing")

    $checks = @(
        Test-Case "gate.default" $defaultGateConfirmed "default gate reports enabled=true runtimeLaunchBehaviorChanged=true persistentDesktopStorageWrites=false"
        Test-Case "gate.force.off" $forceOffGateConfirmed "force-off keeps the gate inactive and the runtime route ownership marker set"
        Test-Case "gate.force.on" $forceOnGateConfirmed "force-on enables the guarded runtime path without changing visible behavior"
        Test-Case "gate.restore.default" $restoreGateConfirmed "the smoke restores the gate to product-default-on before exit"
        Test-Case "off.notepad" $offNotepadFallbackConfirmed "Notepad falls back when the gate is off"
        Test-Case "off.folder" $offFolderOpenFallbackConfirmed "folder desktop icon / file-open path falls back when the gate is off"
        Test-Case "off.text" $offTextOpenFallbackConfirmed "text-file desktop icon / file-open path falls back when the gate is off"
        Test-Case "on.notepad" $onNotepadConfirmed "Notepad is handled by active typed dispatch"
        Test-Case "on.fileexplorer" $onFileExplorerConfirmed "File Explorer is handled by active typed dispatch"
        Test-Case "on.files" $onFilesConfirmed "Files alias is handled by active typed dispatch"
        Test-Case "on.console" $onConsoleConfirmed "Console is handled by active typed dispatch"
        Test-Case "on.calculator" $onCalculatorConfirmed "Calculator is handled by active typed dispatch"
        Test-Case "on.clock" $onClockConfirmed "Clock is handled by active typed dispatch"
        Test-Case "on.taskmanager" $onTaskManagerConfirmed "Task Manager is handled by active typed dispatch"
        Test-Case "on.diskmanager" $onDiskManagerConfirmed "Disk Manager is handled by active typed dispatch"
        Test-Case "on.paint" $onPaintConfirmed "Paint is handled by active typed dispatch"
        Test-Case "on.trash" $onTrashConfirmed "Trash open is handled by active typed dispatch"
        Test-Case "on.controlpanel" $onControlPanelConfirmed "Control Panel shell action is handled by active typed dispatch"
        Test-Case "on.settings" $onSettingsConfirmed "Settings shell action is handled by active typed dispatch"
        Test-Case "on.systemsettings" $onSystemSettingsConfirmed "System Settings shell action is handled by active typed dispatch"
        Test-Case "on.computer" $onComputerConfirmed "Computer shell action is handled by active typed dispatch"
        Test-Case "on.thissystem" $onThisSystemConfirmed "This System shell action is handled by active typed dispatch"
        Test-Case "on.documents" $onDocumentsConfirmed "Documents shell action is handled by active typed dispatch"
        Test-Case "on.pictures" $onPicturesConfirmed "Pictures shell action is handled by active typed dispatch"
        Test-Case "on.music" $onMusicConfirmed "Music shell action is handled by active typed dispatch"
        Test-Case "on.network" $onNetworkConfirmed "Network shell action is handled by active typed dispatch"
        Test-Case "on.folder" $onFolderOpenConfirmed "folder desktop icon / file-open path is handled by active typed dispatch"
        Test-Case "on.text" $onTextOpenConfirmed "text-file desktop icon / file-open path is handled by active typed dispatch"
        Test-Case "fallback.appmodel" $fallbackAppModelConfirmed "AppModel remains a fallback case"
        Test-Case "fallback.unknown" $fallbackUnknownConfirmed "unknown launches remain a fallback case"
    )

    foreach ($check in $checks) {
        $status = if ($check.Pass) { "PASS" } else { "FAIL" }
        Write-Host "check=$($check.Name) status=$status detail=\"$($check.Detail)\""
    }

    $failed = @($checks | Where-Object { -not $_.Pass })
    if ($failed.Count -gt 0) {
        Write-Host ""
        Write-Host "Captured output:"
        Write-Host $output
        throw "Phase 3B guarded active typed dispatch smoke failed: $($failed.Name -join ', ')"
    }

    $phase3BDesktopIconActiveDispatchCovered = $offNotepadFallbackConfirmed -and $offFolderOpenFallbackConfirmed -and $offTextOpenFallbackConfirmed -and $onFolderOpenConfirmed -and $onTextOpenConfirmed -and $onNotepadConfirmed
    $phase3BStartMenuActiveDispatchCovered = $onNotepadConfirmed -and $onFileExplorerConfirmed -and $onFilesConfirmed -and $onConsoleConfirmed -and $onCalculatorConfirmed -and $onClockConfirmed -and $onTaskManagerConfirmed -and $onDiskManagerConfirmed -and $onPaintConfirmed
    $phase3BRightColumnActiveDispatchCovered = $onComputerConfirmed -and $onThisSystemConfirmed -and $onDocumentsConfirmed -and $onPicturesConfirmed -and $onMusicConfirmed -and $onNetworkConfirmed -and $onSettingsConfirmed -and $onControlPanelConfirmed
    $phase3BSystemObjectActiveDispatchCovered = $onFileExplorerConfirmed -and $onComputerConfirmed -and $onThisSystemConfirmed -and $onSystemSettingsConfirmed -and $onTrashConfirmed
    $phase3BUnsupportedTargetsFallback = $fallbackAppModelConfirmed -and $fallbackUnknownConfirmed
    $phase3BRuntimeLaunchBehaviorChanged = $defaultGateConfirmed -and $forceOffGateConfirmed -and $forceOnGateConfirmed -and $restoreGateConfirmed
    $phase3BPersistentDesktopStorageWrites = -not ($defaultGateConfirmed -and $forceOffGateConfirmed -and $forceOnGateConfirmed -and $restoreGateConfirmed)
    $phase3BRestoredDefault = $restoreGateConfirmed -and ($output.Contains("appModelActiveDispatchCurrentState=true"))
    $phase3BTemporarySmokeStateRestored = $phase3BRestoredDefault

    $reportLines = @(
        "[AppModelPhase3BActiveTypedDispatchSmoke]",
        "mode=hosted",
        "flagName=appmodel.active-typed-dispatch",
        "candidateGateName=appmodel.active-typed-dispatch-default-on-candidate",
        "controlledBy=desktop.appmodel.active-typed-dispatch-gate force-on|force-off|reset",
        "candidateModeEnabled=false",
        "effectiveStateSource=product-default",
        "appModelPhase3BDesktopIconActiveDispatchCovered=$($phase3BDesktopIconActiveDispatchCovered.ToString().ToLowerInvariant())",
        "appModelPhase3BStartMenuActiveDispatchCovered=$($phase3BStartMenuActiveDispatchCovered.ToString().ToLowerInvariant())",
        "appModelPhase3BRightColumnActiveDispatchCovered=$($phase3BRightColumnActiveDispatchCovered.ToString().ToLowerInvariant())",
        "appModelPhase3BSystemObjectActiveDispatchCovered=$($phase3BSystemObjectActiveDispatchCovered.ToString().ToLowerInvariant())",
        "appModelPhase3BUnsupportedTargetsFallback=$($phase3BUnsupportedTargetsFallback.ToString().ToLowerInvariant())",
        "appModelPhase3BRuntimeLaunchBehaviorChanged=$($phase3BRuntimeLaunchBehaviorChanged.ToString().ToLowerInvariant())",
        "appModelPhase3BVisibleLaunchBehaviorChanged=false",
        "appModelPhase3BPersistentDesktopStorageWrites=$($phase3BPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
        "runtimeLaunchBehaviorChanged=true",
        "visibleLaunchBehaviorChanged=false",
        "persistentDesktopStorageWrites=false",
        "restoredDefault=$($phase3BRestoredDefault.ToString().ToLowerInvariant())",
        "appModelPhase3BTemporarySmokeStateRestored=$($phase3BTemporarySmokeStateRestored.ToString().ToLowerInvariant())",
        "result=PASS"
    )

    $report = $reportLines -join [Environment]::NewLine
    $logParts = @(
        $report,
        "",
        "[phase3b-output]",
        $output
    )
    Set-Content -Path $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

    Write-Output $report
    Write-Host "Smoke log: $SmokeLog"
}
finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

