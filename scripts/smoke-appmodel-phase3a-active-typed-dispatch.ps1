param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$FixtureDir = Join-Path $Root "tmp\appmodel-phase3a-active-typed-dispatch"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $FixtureDir | Out-Null

if (-not (Test-Path $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$textFile = Join-Path $FixtureDir "phase3a-safe-open.txt"
$folderPath = Join-Path $FixtureDir "phase3a-safe-folder"
New-Item -ItemType Directory -Force -Path $folderPath | Out-Null
Set-Content -Path $textFile -Value @"
Phase 3A active typed dispatch smoke fixture
This file should open through Notepad when guarded active typed dispatch is enabled.
"@ -Encoding ASCII

function Invoke-ServerCommands {
    param([string[]]$Commands)

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return (($inputText | & $Exe 2>&1) -join [Environment]::NewLine)
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

$output = Invoke-ServerCommands -Commands @(
    "gui.start",
    "desktop.appmodel.active-typed-dispatch-gate reset",
    "desktop.appmodel.active-typed-dispatch-gate",
    "desktop.appmodel.active-typed-dispatch-gate force-off",
    "desktop.launch Notepad",
    "desktop.launch AppModel",
    "desktop.appmodel.active-typed-dispatch-gate force-on",
    "desktop.launch Notepad",
    "desktop.launch FileExplorer",
    "desktop.launch Control Panel",
    "desktop.launch System Settings",
    "desktop.open `"$folderPath`" dir",
    "desktop.open `"$textFile`"",
    "desktop.launch AppModel",
    "desktop.launch TotallyUnknownLaunchThing",
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
    $output.Contains("appModelActiveDispatchToggleApplied=false")

$offGateConfirmed =
    $output.Contains("command: desktop.appmodel.active-typed-dispatch-gate") -and
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

$onGateConfirmed =
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

$restoreDefaultConfirmed =
    $output.Contains("mode: reset") -and
    $output.Contains("appModelActiveDispatchPreviousState=true") -and
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

$onControlPanelConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Control Panel classification=ShellAction") -and
    $output.Contains("activeTypedDispatchHandled=true") -and
    $output.Contains("legacyFallbackUsed=false") -and
    $output.Contains("selectedHandler=Control Panel") -and
    $output.Contains("reason=Active typed dispatch handled the Control Panel shell action")

$onSettingsConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=System Settings classification=ShellAction") -and
    $output.Contains("activeTypedDispatchHandled=true") -and
    $output.Contains("legacyFallbackUsed=false") -and
    $output.Contains("selectedHandler=DisplayOptions") -and
    $output.Contains("reason=Active typed dispatch handled Settings through DisplayOptions")

$folderOpenConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=") -and
    $output.Contains($folderPath) -and
    $output.Contains("classification=FileOpen") -and
    $output.Contains("selectedHandler=File Explorer") -and
    $output.Contains("activeTypedDispatchHandled=true") -and
    $output.Contains("legacyFallbackUsed=false") -and
    $output.Contains("reason=Active typed dispatch handled the folder open in File Explorer")

$textOpenConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=") -and
    $output.Contains($textFile) -and
    $output.Contains("classification=FileOpen") -and
    $output.Contains("selectedHandler=Notepad") -and
    $output.Contains("activeTypedDispatchHandled=true") -and
    $output.Contains("legacyFallbackUsed=false") -and
    $output.Contains("reason=Active typed dispatch handled the text-file open in Notepad")

$onAppModelFallbackConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=AppModel classification=LegacyAlias") -and
    $output.Contains("activeTypedDispatchHandled=false") -and
    $output.Contains("legacyFallbackUsed=true") -and
    $output.Contains("selectedHandler=App Model Demo") -and
    $output.Contains("reason=Active typed dispatch is not enabled for this app target")

$unknownFallbackConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=TotallyUnknownLaunchThing classification=Unknown") -and
    $output.Contains("activeTypedDispatchHandled=false") -and
    $output.Contains("legacyFallbackUsed=true") -and
    $output.Contains("selectedHandler=TotallyUnknownLaunchThing") -and
    $output.Contains("reason=Active typed dispatch is not enabled for this app target")

$checks = @(
    Test-Case "gate.default" $defaultGateConfirmed "product-default-on is reported as enabled with runtimeLaunchBehaviorChanged=true and visibleLaunchBehaviorChanged=false"
    Test-Case "gate.off" $offGateConfirmed "force-off disables active typed dispatch and preserves legacy fallback"
    Test-Case "gate.on" $onGateConfirmed "force-on re-enables active typed dispatch without changing visible behavior"
    Test-Case "restore.default" $restoreDefaultConfirmed "reset returns to product-default-on before exit"
    Test-Case "safe.off.fallback" $offNotepadFallbackConfirmed "Notepad falls back when the gate is off"
    Test-Case "safe.on.notepad" $onNotepadConfirmed "Notepad is handled by active typed dispatch"
    Test-Case "safe.on.fileexplorer" $onFileExplorerConfirmed "File Explorer is handled by active typed dispatch"
    Test-Case "safe.on.controlpanel" $onControlPanelConfirmed "Control Panel is handled by active typed dispatch"
    Test-Case "safe.on.settings" $onSettingsConfirmed "System Settings is handled by active typed dispatch"
    Test-Case "safe.on.folder" $folderOpenConfirmed "Folder open is handled by active typed dispatch through File Explorer"
    Test-Case "safe.on.text" $textOpenConfirmed "Text file open is handled by active typed dispatch through Notepad"
    Test-Case "fallback.on.appmodel" $onAppModelFallbackConfirmed "AppModel remains a fallback case"
    Test-Case "fallback.on.unknown" $unknownFallbackConfirmed "Unknown launches remain a fallback case"
)

$failed = @($checks | Where-Object { -not $_.Pass })
foreach ($check in $checks) {
    $status = if ($check.Pass) { "PASS" } else { "FAIL" }
    Write-Host "check=$($check.Name) status=$status detail=\"$($check.Detail)\""
}

if ($failed.Count -gt 0) {
    Write-Host ""
    Write-Host "Captured output:"
    Write-Host $output
    throw "Phase 3A guarded active typed dispatch smoke failed: $($failed.Name -join ', ')"
}

Write-Host ""
Write-Host "[AppModelPhase3AActiveTypedDispatchSmoke]"
Write-Host "result=PASS"
Write-Host "flagName=appmodel.active-typed-dispatch"
Write-Host "candidateGateName=appmodel.active-typed-dispatch-default-on-candidate"
Write-Host "controlledBy=desktop.appmodel.active-typed-dispatch-gate force-on|force-off|reset"
Write-Host "safeCases=Notepad,FileExplorer,Control Panel,System Settings,FolderOpen,TextFileOpen"
Write-Host "fallbackCases=AppModel,TotallyUnknownLaunchThing"
Write-Host "candidateModeEnabled=false"
Write-Host "effectiveStateSource=product-default"
Write-Host "runtimeLaunchBehaviorChanged=true"
Write-Host "visibleLaunchBehaviorChanged=false"
Write-Host "persistentDesktopStorageWrites=false"
Write-Host "restoredDefault=true"

