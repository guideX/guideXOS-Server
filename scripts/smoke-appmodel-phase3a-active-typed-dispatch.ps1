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

$output = Invoke-ServerCommands -Commands @(
    "gui.start",
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
    "desktop.appmodel.active-typed-dispatch-gate force-off",
    "desktop.appmodel.active-typed-dispatch-gate"
)

$offGateConfirmed =
    $output.Contains("command: desktop.appmodel.active-typed-dispatch-gate") -and
    $output.Contains("mode: force-off") -and
    $output.Contains("appModelActiveDispatchEnabled=false") -and
    $output.Contains("appModelActiveDispatchCurrentState=false") -and
    $output.Contains("runtimeLaunchBehaviorChanged=false") -and
    $output.Contains("persistentDesktopStorageWrites=false") -and
    $output.Contains("appModelActiveDispatchToggleApplied=true")

$onGateConfirmed =
    $output.Contains("mode: force-on") -and
    $output.Contains("appModelActiveDispatchEnabled=true") -and
    $output.Contains("appModelActiveDispatchCurrentState=true") -and
    $output.Contains("runtimeLaunchBehaviorChanged=false") -and
    $output.Contains("persistentDesktopStorageWrites=false") -and
    $output.Contains("appModelActiveDispatchToggleApplied=true")

$offNotepadFallbackConfirmed =
    $output.Contains("[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp") -and
    $output.Contains("activeTypedDispatchHandled=false") -and
    $output.Contains("legacyFallbackUsed=true") -and
    $output.Contains("visibleBehaviorChanged=false") -and
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

$restoreOffConfirmed =
    $output.Contains("mode: force-off") -and
    $output.Contains("appModelActiveDispatchPreviousState=true") -and
    $output.Contains("appModelActiveDispatchCurrentState=false") -and
    $output.Contains("nonFatal=true")

$checks = @(
    @{ Name = "gate.off"; Pass = $offGateConfirmed; Detail = "default flag off is preserved and the gate reports runtimeLaunchBehaviorChanged=false persistentDesktopStorageWrites=false" },
    @{ Name = "gate.on"; Pass = $onGateConfirmed; Detail = "smoke-only switch turns guarded active typed dispatch on and still reports runtimeLaunchBehaviorChanged=false persistentDesktopStorageWrites=false" },
    @{ Name = "safe.off.fallback"; Pass = $offNotepadFallbackConfirmed; Detail = "flag-off Notepad launch falls back to the legacy path" },
    @{ Name = "safe.on.notepad"; Pass = $onNotepadConfirmed; Detail = "Notepad is handled by guarded active typed dispatch" },
    @{ Name = "safe.on.fileexplorer"; Pass = $onFileExplorerConfirmed; Detail = "File Explorer is handled by guarded active typed dispatch" },
    @{ Name = "safe.on.controlpanel"; Pass = $onControlPanelConfirmed; Detail = "Control Panel is handled by guarded active typed dispatch" },
    @{ Name = "safe.on.settings"; Pass = $onSettingsConfirmed; Detail = "System Settings is handled by guarded active typed dispatch" },
    @{ Name = "safe.on.folder"; Pass = $folderOpenConfirmed; Detail = "Folder open is handled by guarded active typed dispatch through FileExplorer" },
    @{ Name = "safe.on.text"; Pass = $textOpenConfirmed; Detail = "Text file open is handled by guarded active typed dispatch through Notepad" },
    @{ Name = "fallback.on.appmodel"; Pass = $onAppModelFallbackConfirmed; Detail = "AppModel remains a fallback case" },
    @{ Name = "fallback.on.unknown"; Pass = $unknownFallbackConfirmed; Detail = "Unknown launches remain a fallback case" },
    @{ Name = "restore.off"; Pass = $restoreOffConfirmed; Detail = "the gate is restored to force-off before exit" }
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
Write-Host "controlledBy=desktop.appmodel.active-typed-dispatch-gate force-on|force-off"
Write-Host "safeCases=Notepad,FileExplorer,Control Panel,System Settings,FolderOpen,TextFileOpen"
Write-Host "fallbackCases=AppModel,TotallyUnknownLaunchThing"
Write-Host "runtimeLaunchBehaviorChanged=false"
Write-Host "persistentDesktopStorageWrites=false"
Write-Host "restoredOff=true"
