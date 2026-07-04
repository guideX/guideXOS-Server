param(
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase3c-active-typed-dispatch"
$BackupRoot = Join-Path $TempRoot "restore-backup"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

if (-not (Test-Path $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$FixtureFolder = Join-Path $TempRoot "phase3c-safe-folder"
$FixtureText = Join-Path $TempRoot "phase3c-safe-open.txt"
$SmokeLog = Join-Path $LogDir "appmodel-phase3c-active-typed-dispatch-$stamp.log"

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
Phase 3C active typed dispatch smoke fixture
This file should open through Notepad when guarded active typed dispatch is enabled.
"@ -Encoding ASCII

    $output = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.appmodel.active-typed-dispatch-gate",
        "desktop.launch Notepad",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`"",
        "desktop.appmodel.active-typed-dispatch-default-on-candidate candidate-on",
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
        "desktop.appmodel.active-typed-dispatch-default-on-candidate candidate-off",
        "desktop.appmodel.active-typed-dispatch-gate reset",
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

    $candidateGateConfirmed =
        $output.Contains("command: desktop.appmodel.active-typed-dispatch-default-on-candidate") -and
        $output.Contains("mode: candidate-on") -and
        $output.Contains("appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=true") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchRuntimePath=active") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $forceOffGateConfirmed =
        $output.Contains("command: desktop.appmodel.active-typed-dispatch-gate") -and
        $output.Contains("mode: force-off") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=true") -and
        $output.Contains("appModelActiveDispatchEnabled=false") -and
        $output.Contains("appModelActiveDispatchCurrentState=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $forceOnGateConfirmed =
        $output.Contains("mode: force-on") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=true") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchCurrentState=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("runtimeLaunchBehaviorChanged=true") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("persistentDesktopStorageWrites=false") -and
        $output.Contains("appModelActiveDispatchToggleApplied=true")

    $restoreGateConfirmed =
        $output.Contains("mode: reset") -and
        $output.Contains("appModelActiveDispatchCandidateEnabled=false") -and
        $output.Contains("appModelActiveDispatchEnabled=true") -and
        $output.Contains("appModelActiveDispatchCurrentState=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("nonFatal=true")

    $defaultOffNotepadFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $defaultOffFolderOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $defaultOffTextOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("visibleLaunchBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=product-default") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $candidateOnNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $candidateOnFileExplorerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=FileExplorer classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $candidateOnFilesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Files classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the File Explorer launch")

    $candidateOnConsoleConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Console classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Console") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Console launch")

    $candidateOnCalculatorConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Calculator classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Calculator") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Calculator launch")

    $candidateOnClockConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Clock classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Clock") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Clock launch")

    $candidateOnTaskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TaskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=TaskManager") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Task Manager launch")

    $candidateOnDiskManagerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=DiskManager classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DiskManager") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Disk Manager launch")

    $candidateOnPaintConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Paint classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Paint") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Paint launch")

    $candidateOnTrashConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Trash classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Trash") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Trash launch")

    $candidateOnControlPanelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Control Panel classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Control Panel") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the Control Panel shell action")

    $candidateOnSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $candidateOnSystemSettingsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=System Settings classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=DisplayOptions") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

    $candidateOnComputerConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Computer classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $candidateOnThisSystemConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=This System classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the root shell object in File Explorer")

    $candidateOnDocumentsConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Documents classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $candidateOnPicturesConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Pictures classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $candidateOnMusicConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Music classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $candidateOnNetworkConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Network classification=ShellAction") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the folder shell action in File Explorer")

    $candidateOnFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $candidateOnTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $candidateFallbackAppModelConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=AppModel classification=LegacyAlias") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("selectedHandler=App Model Demo")

    $candidateFallbackUnknownConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TotallyUnknownLaunchThing classification=Unknown") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=compatibility/candidate") -and
        $output.Contains("selectedHandler=TotallyUnknownLaunchThing")

    $forceOffNotepadFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOffFolderOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOffTextOpenFallbackConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("activeTypedDispatchHandled=false") -and
        $output.Contains("legacyFallbackUsed=true") -and
        $output.Contains("visibleBehaviorChanged=false") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-off") -and
        $output.Contains("reason=Active typed dispatch gate is disabled")

    $forceOnAgainNotepadConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the Notepad launch")

    $forceOnAgainFolderOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=File Explorer") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

    $forceOnAgainTextOpenConfirmed =
        $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen") -and
        $output.Contains("activeTypedDispatchHandled=true") -and
        $output.Contains("legacyFallbackUsed=false") -and
        $output.Contains("selectedHandler=Notepad") -and
        $output.Contains("appModelActiveDispatchEffectiveStateSource=force-on") -and
        $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

    $checks = @(
        Test-Case "gate.default" $defaultGateConfirmed "default gate reports enabled=true runtimeLaunchBehaviorChanged=true persistentDesktopStorageWrites=false"
        Test-Case "gate.candidate.on" $candidateGateConfirmed "candidate mode turns active typed dispatch on and reports compatibility/candidate source"
        Test-Case "gate.force.off" $forceOffGateConfirmed "force-off overrides candidate compatibility state and keeps the runtime behavior unchanged"
        Test-Case "gate.force.on" $forceOnGateConfirmed "force-on overrides candidate compatibility state and re-enables the guarded runtime path"
        Test-Case "gate.restore.default" $restoreGateConfirmed "the smoke restores candidate mode to product-default-on before exit"
        Test-Case "default.on.notepad" $defaultOffNotepadFallbackConfirmed "Notepad is handled before candidate mode is enabled"
        Test-Case "default.on.folder" $defaultOffFolderOpenFallbackConfirmed "folder desktop icon / file-open path is handled before candidate mode is enabled"
        Test-Case "default.on.text" $defaultOffTextOpenFallbackConfirmed "text-file desktop icon / file-open path is handled before candidate mode is enabled"
        Test-Case "candidate.on.notepad" $candidateOnNotepadConfirmed "Notepad is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.fileexplorer" $candidateOnFileExplorerConfirmed "File Explorer is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.files" $candidateOnFilesConfirmed "Files alias is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.console" $candidateOnConsoleConfirmed "Console is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.calculator" $candidateOnCalculatorConfirmed "Calculator is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.clock" $candidateOnClockConfirmed "Clock is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.taskmanager" $candidateOnTaskManagerConfirmed "Task Manager is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.diskmanager" $candidateOnDiskManagerConfirmed "Disk Manager is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.paint" $candidateOnPaintConfirmed "Paint is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.trash" $candidateOnTrashConfirmed "Trash open is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.controlpanel" $candidateOnControlPanelConfirmed "Control Panel shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.settings" $candidateOnSettingsConfirmed "Settings shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.systemsettings" $candidateOnSystemSettingsConfirmed "System Settings shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.computer" $candidateOnComputerConfirmed "Computer shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.thissystem" $candidateOnThisSystemConfirmed "This System shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.documents" $candidateOnDocumentsConfirmed "Documents shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.pictures" $candidateOnPicturesConfirmed "Pictures shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.music" $candidateOnMusicConfirmed "Music shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.network" $candidateOnNetworkConfirmed "Network shell action is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.folder" $candidateOnFolderOpenConfirmed "folder desktop icon / file-open path is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.on.text" $candidateOnTextOpenConfirmed "text-file desktop icon / file-open path is handled by active typed dispatch under candidate default-on mode"
        Test-Case "candidate.fallback.appmodel" $candidateFallbackAppModelConfirmed "AppModel remains a fallback case under candidate mode"
        Test-Case "candidate.fallback.unknown" $candidateFallbackUnknownConfirmed "unknown launches remain a fallback case under candidate mode"
        Test-Case "force.off.notepad" $forceOffNotepadFallbackConfirmed "Notepad falls back when force-off is applied during candidate mode"
        Test-Case "force.off.folder" $forceOffFolderOpenFallbackConfirmed "folder desktop icon / file-open path falls back when force-off is applied during candidate mode"
        Test-Case "force.off.text" $forceOffTextOpenFallbackConfirmed "text-file desktop icon / file-open path falls back when force-off is applied during candidate mode"
        Test-Case "force.on.again.notepad" $forceOnAgainNotepadConfirmed "Notepad is handled again after force-on re-enables active typed dispatch"
        Test-Case "force.on.again.folder" $forceOnAgainFolderOpenConfirmed "folder open is handled again after force-on re-enables active typed dispatch"
        Test-Case "force.on.again.text" $forceOnAgainTextOpenConfirmed "text-file open is handled again after force-on re-enables active typed dispatch"
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
        throw "Phase 3C default-on candidate active typed dispatch smoke failed: $($failed.Name -join ', ')"
    }

    $phase3CDefaultOnCandidateAvailable = $true
    $phase3CDefaultOnCandidateEnabled = $candidateGateConfirmed
    $phase3CActiveDispatchEffectiveDefault = $candidateGateConfirmed
    $phase3CEmergencyForceOffWorks = $forceOffGateConfirmed -and $forceOffNotepadFallbackConfirmed -and $forceOffFolderOpenFallbackConfirmed -and $forceOffTextOpenFallbackConfirmed
    $phase3CForceOnWorks = $forceOnGateConfirmed -and $forceOnAgainNotepadConfirmed -and $forceOnAgainFolderOpenConfirmed -and $forceOnAgainTextOpenConfirmed
    $phase3CLegacyFallbackStillAvailable = $defaultOffNotepadFallbackConfirmed -and $candidateOnNotepadConfirmed -and $forceOffNotepadFallbackConfirmed -and $candidateFallbackAppModelConfirmed
    $phase3CUnsupportedTargetsFallback = $candidateFallbackAppModelConfirmed -and $candidateFallbackUnknownConfirmed
    $phase3CRuntimeLaunchBehaviorChanged = $defaultGateConfirmed -and $candidateGateConfirmed -and $forceOffGateConfirmed -and $forceOnGateConfirmed -and $restoreGateConfirmed
    $phase3CPersistentDesktopStorageWrites = -not ($defaultGateConfirmed -and $candidateGateConfirmed -and $forceOffGateConfirmed -and $forceOnGateConfirmed -and $restoreGateConfirmed)
    $phase3CTemporarySmokeStateRestored = $restoreGateConfirmed -and ($output.Contains("appModelActiveDispatchCandidateEnabled=false")) -and ($output.Contains("appModelActiveDispatchEffectiveStateSource=product-default"))

    $reportLines = @(
        "[AppModelPhase3CDefaultOnCandidateSmoke]",
        "mode=hosted",
        "flagName=appmodel.active-typed-dispatch",
        "candidateGateName=appmodel.active-typed-dispatch-default-on-candidate",
        "controlledBy=desktop.appmodel.active-typed-dispatch-gate force-on|force-off|reset plus desktop.appmodel.active-typed-dispatch-default-on-candidate on|off|reset",
        "appModelPhase3CDefaultOnCandidateAvailable=$($phase3CDefaultOnCandidateAvailable.ToString().ToLowerInvariant())",
        "appModelPhase3CDefaultOnCandidateEnabled=$($phase3CDefaultOnCandidateEnabled.ToString().ToLowerInvariant())",
        "appModelPhase3CDefaultOnCandidateEnabledRestored=$((-not $phase3CDefaultOnCandidateEnabled).ToString().ToLowerInvariant())",
        "appModelPhase3CActiveDispatchEffectiveDefault=$($phase3CActiveDispatchEffectiveDefault.ToString().ToLowerInvariant())",
        "appModelPhase3CActiveDispatchEffectiveDefaultRestored=$((-not $phase3CActiveDispatchEffectiveDefault).ToString().ToLowerInvariant())",
        "appModelPhase3CEmergencyForceOffWorks=$($phase3CEmergencyForceOffWorks.ToString().ToLowerInvariant())",
        "appModelPhase3CForceOnWorks=$($phase3CForceOnWorks.ToString().ToLowerInvariant())",
        "appModelPhase3CLegacyFallbackStillAvailable=$($phase3CLegacyFallbackStillAvailable.ToString().ToLowerInvariant())",
        "appModelPhase3CUnsupportedTargetsFallback=$($phase3CUnsupportedTargetsFallback.ToString().ToLowerInvariant())",
        "appModelPhase3CRuntimeLaunchBehaviorChanged=$($phase3CRuntimeLaunchBehaviorChanged.ToString().ToLowerInvariant())",
        "appModelPhase3CPersistentDesktopStorageWrites=$($phase3CPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
        "runtimeLaunchBehaviorChanged=true",
        "persistentDesktopStorageWrites=false",
        "appModelPhase3CTemporarySmokeStateRestored=$($phase3CTemporarySmokeStateRestored.ToString().ToLowerInvariant())",
        "result=PASS"
    )

    $report = $reportLines -join [Environment]::NewLine
    $logParts = @(
        $report,
        "",
        "[phase3c-output]",
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
if ($cleanupVerified) {
    Write-Host "appModelPhase3CTemporaryArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase3CTemporaryArtifactsCleaned=false"
}


