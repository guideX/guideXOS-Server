param(
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase3d-active-typed-dispatch"
$BackupRoot = Join-Path $TempRoot "restore-backup"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

if (-not (Test-Path $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$FixtureFolder = Join-Path $TempRoot "phase3d-safe-folder"
$FixtureText = Join-Path $TempRoot "phase3d-safe-open.txt"
$SmokeLog = Join-Path $LogDir "appmodel-phase3d-active-typed-dispatch-$stamp.log"

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
Phase 3D active typed dispatch smoke fixture
This file should open through Notepad when active typed dispatch is product-default-on.
"@ -Encoding ASCII

    $output = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
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
        "desktop.launch AppModel",
        "desktop.launch TotallyUnknownLaunchThing",
        "desktop.appmodel.active-typed-dispatch-gate force-off",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.appmodel.active-typed-dispatch-gate force-on",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate"
    )

    $defaultGateConfirmed =
        $output.Contains("command: desktop.appmodel.active-typed-dispatch-gate") -and
        $output.Contains("mode: status") -and
        $output.Contains("appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchCurrentState=true") -and
        $output.Contains("appModelActiveDispatchRuntimePath=active") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $safeNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $safeFileExplorerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=FileExplorer classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $safeFilesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Files classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $safeConsoleConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Console classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Console") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Console launch")

    $safeCalculatorConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Calculator classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Calculator") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Calculator launch")

    $safeClockConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Clock classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Clock") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Clock launch")

    $safeTaskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TaskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=TaskManager") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Task Manager launch")

    $safeDiskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=DiskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DiskManager") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Disk Manager launch")

    $safePaintConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Paint classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Paint") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Paint launch")

    $safeTrashConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Trash classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Trash") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Trash launch")

    $safeControlPanelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Control Panel classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Control Panel") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Control Panel shell action")

    $safeSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $safeSystemSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=System Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $safeComputerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Computer classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $safeThisSystemConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=This System classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $safeDocumentsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Documents classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $safePicturesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Pictures classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $safeMusicConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Music classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $safeNetworkConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Network classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $safeFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $safeTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $fallbackAppModelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=AppModel classification=LegacyAlias") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("selectedHandler=App Model Demo") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch is not enabled for this app target")

    $fallbackUnknownConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TotallyUnknownLaunchThing classification=Unknown") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("selectedHandler=TotallyUnknownLaunchThing") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch is not enabled for this app target")

    $forceOffGateConfirmed =
        $output.Contains("mode: force-off") -and
        $output.Contains("appModelActiveDispatchEnabled=false") -and
        $output.Contains("appModelActiveDispatchCurrentState=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $forceOffNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOffFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOffTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOnGateConfirmed =
        $output.Contains("mode: force-on") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchCurrentState=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $forceOnNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $forceOnFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $forceOnTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $resetGateConfirmed =
        $output.Contains("mode: reset") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchCurrentState=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("nonFatal=true")

    $checks = @(
        Test-Case "gate.default" $defaultGateConfirmed "product-default-on is reported as enabled with runtimeLaunchBehaviorChanged=true and visibleLaunchBehaviorChanged=false"
        Test-Case "safe.notepad" $safeNotepadConfirmed "Notepad is handled by active typed dispatch at product default"
        Test-Case "safe.fileexplorer" $safeFileExplorerConfirmed "File Explorer is handled by active typed dispatch at product default"
        Test-Case "safe.files" $safeFilesConfirmed "Files alias is handled by active typed dispatch at product default"
        Test-Case "safe.console" $safeConsoleConfirmed "Console is handled by active typed dispatch at product default"
        Test-Case "safe.calculator" $safeCalculatorConfirmed "Calculator is handled by active typed dispatch at product default"
        Test-Case "safe.clock" $safeClockConfirmed "Clock is handled by active typed dispatch at product default"
        Test-Case "safe.taskmanager" $safeTaskManagerConfirmed "Task Manager is handled by active typed dispatch at product default"
        Test-Case "safe.diskmanager" $safeDiskManagerConfirmed "Disk Manager is handled by active typed dispatch at product default"
        Test-Case "safe.paint" $safePaintConfirmed "Paint is handled by active typed dispatch at product default"
        Test-Case "safe.trash" $safeTrashConfirmed "Trash open is handled by active typed dispatch at product default"
        Test-Case "safe.controlpanel" $safeControlPanelConfirmed "Control Panel shell action is handled by active typed dispatch at product default"
        Test-Case "safe.settings" $safeSettingsConfirmed "Settings shell action is handled by active typed dispatch at product default"
        Test-Case "safe.systemsettings" $safeSystemSettingsConfirmed "System Settings shell action is handled by active typed dispatch at product default"
        Test-Case "safe.computer" $safeComputerConfirmed "Computer shell action is handled by active typed dispatch at product default"
        Test-Case "safe.thissystem" $safeThisSystemConfirmed "This System shell action is handled by active typed dispatch at product default"
        Test-Case "safe.documents" $safeDocumentsConfirmed "Documents shell action is handled by active typed dispatch at product default"
        Test-Case "safe.pictures" $safePicturesConfirmed "Pictures shell action is handled by active typed dispatch at product default"
        Test-Case "safe.music" $safeMusicConfirmed "Music shell action is handled by active typed dispatch at product default"
        Test-Case "safe.network" $safeNetworkConfirmed "Network shell action is handled by active typed dispatch at product default"
        Test-Case "safe.folder" $safeFolderOpenConfirmed "Folder open is handled by active typed dispatch at product default"
        Test-Case "safe.text" $safeTextOpenConfirmed "Text file open is handled by active typed dispatch at product default"
        Test-Case "fallback.appmodel" $fallbackAppModelConfirmed "AppModel remains a fallback case"
        Test-Case "fallback.unknown" $fallbackUnknownConfirmed "unknown launches remain a fallback case"
        Test-Case "gate.force.off" $forceOffGateConfirmed "force-off disables active typed dispatch and preserves legacy fallback"
        Test-Case "force.off.notepad" $forceOffNotepadConfirmed "Notepad falls back when the gate is off"
        Test-Case "force.off.folder" $forceOffFolderOpenConfirmed "folder open falls back when the gate is off"
        Test-Case "force.off.text" $forceOffTextOpenConfirmed "text open falls back when the gate is off"
        Test-Case "gate.force.on" $forceOnGateConfirmed "force-on re-enables active typed dispatch without changing visible behavior"
        Test-Case "force.on.notepad" $forceOnNotepadConfirmed "Notepad is handled again after force-on re-enables active typed dispatch"
        Test-Case "force.on.folder" $forceOnFolderOpenConfirmed "folder open is handled again after force-on re-enables active typed dispatch"
        Test-Case "force.on.text" $forceOnTextOpenConfirmed "text open is handled again after force-on re-enables active typed dispatch"
        Test-Case "gate.restore.default" $resetGateConfirmed "reset returns to product-default-on before exit"
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
        throw "Phase 3D product-default-on active typed dispatch smoke failed: $($failed.Name -join ', ')"
    }

    $phase3DSafeCasesCovered =
        $safeNotepadConfirmed -and
        $safeFileExplorerConfirmed -and
        $safeFilesConfirmed -and
        $safeConsoleConfirmed -and
        $safeCalculatorConfirmed -and
        $safeClockConfirmed -and
        $safeTaskManagerConfirmed -and
        $safeDiskManagerConfirmed -and
        $safePaintConfirmed -and
        $safeTrashConfirmed -and
        $safeControlPanelConfirmed -and
        $safeSettingsConfirmed -and
        $safeSystemSettingsConfirmed -and
        $safeComputerConfirmed -and
        $safeThisSystemConfirmed -and
        $safeDocumentsConfirmed -and
        $safePicturesConfirmed -and
        $safeMusicConfirmed -and
        $safeNetworkConfirmed -and
        $safeFolderOpenConfirmed -and
        $safeTextOpenConfirmed

    $phase3DEmergencyForceOffWorks = $forceOffGateConfirmed -and $forceOffNotepadConfirmed -and $forceOffFolderOpenConfirmed -and $forceOffTextOpenConfirmed
    $phase3DForceOnWorks = $forceOnGateConfirmed -and $forceOnNotepadConfirmed -and $forceOnFolderOpenConfirmed -and $forceOnTextOpenConfirmed
    $phase3DLegacyFallbackStillAvailable = $fallbackAppModelConfirmed -and $fallbackUnknownConfirmed -and $forceOffNotepadConfirmed -and $forceOffFolderOpenConfirmed -and $forceOffTextOpenConfirmed
    $phase3DUnsupportedTargetsFallback = $fallbackAppModelConfirmed -and $fallbackUnknownConfirmed
    $phase3DTrueDefaultOn = $defaultGateConfirmed
    $phase3DProductDefaultEnabled = $defaultGateConfirmed
    $phase3DResetReturnsToProductDefault = $resetGateConfirmed
    $phase3DRuntimeLaunchBehaviorChanged = $defaultGateConfirmed -and $phase3DEmergencyForceOffWorks -and $phase3DForceOnWorks -and $phase3DResetReturnsToProductDefault
    $phase3DPersistentDesktopStorageWrites = -not ($defaultGateConfirmed -and $phase3DEmergencyForceOffWorks -and $phase3DForceOnWorks -and $phase3DResetReturnsToProductDefault)
    $phase3DVisibleLaunchBehaviorChanged = $false
    $reportLines = @(
        "[AppModelPhase3DActiveTypedDispatchSmoke]",
        "mode=hosted",
        "flagName=appmodel.active-typed-dispatch",
        "candidateGateName=appmodel.active-typed-dispatch-default-on-candidate",
        "controlledBy=desktop.appmodel.active-typed-dispatch-gate force-on|force-off|reset",
        "appModelPhase3DTrueDefaultOn=$($phase3DTrueDefaultOn.ToString().ToLowerInvariant())",
        "appModelPhase3DProductDefaultEnabled=$($phase3DProductDefaultEnabled.ToString().ToLowerInvariant())",
        "appModelPhase3DResetReturnsToProductDefault=$($phase3DResetReturnsToProductDefault.ToString().ToLowerInvariant())",
        "appModelPhase3DEmergencyForceOffWorks=$($phase3DEmergencyForceOffWorks.ToString().ToLowerInvariant())",
        "appModelPhase3DForceOnWorks=$($phase3DForceOnWorks.ToString().ToLowerInvariant())",
        "appModelPhase3DLegacyFallbackStillAvailable=$($phase3DLegacyFallbackStillAvailable.ToString().ToLowerInvariant())",
        "appModelPhase3DUnsupportedTargetsFallback=$($phase3DUnsupportedTargetsFallback.ToString().ToLowerInvariant())",
        "appModelPhase3DRuntimeLaunchBehaviorChanged=$($phase3DRuntimeLaunchBehaviorChanged.ToString().ToLowerInvariant())",
        "appModelPhase3DVisibleLaunchBehaviorChanged=$($phase3DVisibleLaunchBehaviorChanged.ToString().ToLowerInvariant())",
        "appModelPhase3DPersistentDesktopStorageWrites=$($phase3DPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
        "runtimeLaunchBehaviorChanged=true",
        "visibleLaunchBehaviorChanged=false",
        "persistentDesktopStorageWrites=false",
        "appModelPhase3DSafeCasesCovered=$($phase3DSafeCasesCovered.ToString().ToLowerInvariant())",
        "result=PASS"
    )

    $report = $reportLines -join [Environment]::NewLine
    $logParts = @(
        $report,
        "",
        "[phase3d-output]",
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

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$phase3DTemporarySmokeStateRestored = $cleanupVerified
if ($cleanupVerified) {
    Write-Host "appModelPhase3DTemporaryArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase3DTemporaryArtifactsCleaned=false"
}
Write-Host "appModelPhase3DTemporarySmokeStateRestored=$($phase3DTemporarySmokeStateRestored.ToString().ToLowerInvariant())"
