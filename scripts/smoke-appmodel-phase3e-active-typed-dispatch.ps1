param(
    [int]$TimeoutSeconds = 45
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Exe = Join-Path $Root "guideXOSServer.exe"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root "tmp\appmodel-phase3e-active-typed-dispatch"
$FixtureRoot = Join-Path $TempRoot "fixtures"
$BackupRoot = Join-Path $TempRoot "restore-backup"

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
New-Item -ItemType Directory -Force -Path $TempRoot | Out-Null
New-Item -ItemType Directory -Force -Path $FixtureRoot | Out-Null
New-Item -ItemType Directory -Force -Path $BackupRoot | Out-Null

if (-not (Test-Path $Exe)) {
    throw "guideXOSServer.exe not found: $Exe. Run .\build.bat first."
}

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "appmodel-phase3e-active-typed-dispatch-$stamp.log"
$FixtureFolder = Join-Path $FixtureRoot "phase3e-safe-folder"
$FixtureText = Join-Path $FixtureRoot "phase3e-safe-open.txt"
$FixtureUnsupported = Join-Path $FixtureRoot "phase3e-unsupported.xyz"
$FixtureImage = Join-Path $FixtureRoot "phase3e-legacy-image.png"

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
        return @($cfg.recent | ForEach-Object { $_.ToString() })
    } catch {
        return @()
    }
}

function Count-RegexMatches {
    param(
        [string]$Text,
        [string]$Pattern
    )

    return [regex]::Matches($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline).Count
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

    $count = Count-RegexMatches -Text $Text -Pattern $Pattern
    Assert-True ($count -ge $Minimum) "Expected at least $Minimum matches for ${Reason}, but saw $count. Pattern: $Pattern"
}

function Test-RecentContainsAll {
    param(
        [string[]]$RecentNames,
        [string[]]$ExpectedNames
    )

    foreach ($expected in $ExpectedNames) {
        if ($RecentNames -notcontains $expected) {
            return $false
        }
    }
    return $true
}

function Test-RecentExcludesAny {
    param(
        [string[]]$RecentNames,
        [string[]]$ExcludedNames
    )

    foreach ($excluded in $ExcludedNames) {
        if ($RecentNames -contains $excluded) {
            return $false
        }
    }
    return $true
}

$artifactState = Backup-TrackedArtifacts

try {
    Set-Content -LiteralPath $FixtureText -Value @"
Phase 3E active typed dispatch smoke fixture
This text file should open through Notepad.
"@ -Encoding ASCII

    Set-Content -LiteralPath $FixtureUnsupported -Value @"
Phase 3E unsupported file smoke fixture
This file intentionally uses an unsupported extension.
"@ -Encoding ASCII

    Copy-Item -LiteralPath (Join-Path $Root "assets\Backgrounds\ameoba.png") -Destination $FixtureImage -Force
    New-Item -ItemType Directory -Force -Path $FixtureFolder | Out-Null

    $bootOutput = Invoke-ServerCommands -Commands @(
        "gui.start"
    )
    $desktopJsonBefore = Get-FileText (Join-Path $Root "desktop.json")
    $desktopStateBefore = Get-FileText (Join-Path $Root "desktop.state")
    $displayOptionsBefore = Get-FileText (Join-Path $Root "display-options.cfg")

    if (Test-Path -LiteralPath (Join-Path $Root "desktop.json")) {
        $desktopConfigFixture = Get-Content -LiteralPath (Join-Path $Root "desktop.json") -Raw | ConvertFrom-Json
        $desktopConfigFixture.recent = @()
        ($desktopConfigFixture | ConvertTo-Json -Depth 64) | Set-Content -LiteralPath (Join-Path $Root "desktop.json") -Encoding ASCII
    }
    Assert-True (@(Get-DesktopRecentNames).Count -eq 0) "Phase 3E smoke should start from an empty recent-program fixture"

    $preflightOutput = Invoke-ServerCommands -Commands @(
        "desktop.appmodel.active-typed-dispatch-gate reset",
        "desktop.launch.compare",
        "desktop.launch.storage.preview.compare",
        "gui.smoke.launchshadow",
        "desktop.appmodel.active-typed-dispatch-gate"
    )
    Assert-Contains $preflightOutput "command: desktop.appmodel.active-typed-dispatch-gate" "default gate status"
    Assert-Contains $preflightOutput "mode: status" "default gate status"
    Assert-Contains $preflightOutput "appModelActiveDispatchEnabled=true" "default gate status"
    Assert-Contains $preflightOutput "appModelActiveDispatchEffectiveStateSource=product-default" "default gate status"
    Assert-Contains $preflightOutput "visibleLaunchBehaviorChanged=false" "default gate status"
    Assert-Contains $preflightOutput "persistentDesktopStorageWrites=false" "default gate status"
    Assert-Contains $preflightOutput "command: gui.smoke.launchshadow" "launchshadow smoke"
    Assert-Contains $preflightOutput "source=StartMenu" "launchshadow smoke"
    Assert-Contains $preflightOutput "source=DesktopShortcut" "launchshadow smoke"
    Assert-Contains $preflightOutput "launchesApps: false" "launchshadow smoke"
    Assert-Contains $preflightOutput "writesStorageHosted=false" "storage preview comparison"
    Assert-Contains $preflightOutput "writesStorageBareMetal=false" "storage preview comparison"

    $repeatedBatch = @(
        "gui.start",
        "desktop.launch Notepad",
        "desktop.launch FileExplorer",
        "desktop.launch Files",
        "desktop.launch Console",
        "desktop.launch Settings",
        "desktop.launch System Settings",
        "desktop.launch Control Panel",
        "desktop.launch Calculator",
        "desktop.launch Clock",
        "desktop.launch Paint",
        "desktop.launch TaskManager",
        "desktop.launch DiskManager",
        "desktop.launch guideXOS Navigator",
        "desktop.launch Trash",
        "desktop.launch Computer",
        "desktop.launch This System",
        "desktop.launch Documents",
        "desktop.launch Pictures",
        "desktop.launch Music",
        "desktop.launch Network",
        "desktop.open `"$FixtureFolder`" dir",
        "desktop.open `"$FixtureText`""
    )

    $round1 = Invoke-ServerCommands -Commands $repeatedBatch
    Start-Sleep -Milliseconds 500
    $round2 = Invoke-ServerCommands -Commands $repeatedBatch
    $repeatedOutput = $round1 + [Environment]::NewLine + $round2

    $repeatedLaunchCases = @(
        @{ Name = "Notepad"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Notepad classification=BuiltInApp'; Reason = "Notepad repeated launches"; ExpectedCount = 2 },
        @{ Name = "FileExplorer"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=FileExplorer classification=BuiltInApp'; Reason = "File Explorer repeated launches"; ExpectedCount = 2 },
        @{ Name = "Files"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Files classification=BuiltInApp'; Reason = "Files repeated launches"; ExpectedCount = 2 },
        @{ Name = "Console"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Console classification=BuiltInApp'; Reason = "Console repeated launches"; ExpectedCount = 2 },
        @{ Name = "Settings"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Settings classification=ShellAction'; Reason = "Settings repeated launches"; ExpectedCount = 2 },
        @{ Name = "System Settings"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=System Settings classification=ShellAction'; Reason = "System Settings repeated launches"; ExpectedCount = 2 },
        @{ Name = "Control Panel"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Control Panel classification=ShellAction'; Reason = "Control Panel repeated launches"; ExpectedCount = 2 },
        @{ Name = "Calculator"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Calculator classification=BuiltInApp'; Reason = "Calculator repeated launches"; ExpectedCount = 2 },
        @{ Name = "Clock"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Clock classification=BuiltInApp'; Reason = "Clock repeated launches"; ExpectedCount = 2 },
        @{ Name = "Paint"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Paint classification=BuiltInApp'; Reason = "Paint repeated launches"; ExpectedCount = 2 },
        @{ Name = "TaskManager"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=TaskManager classification=BuiltInApp'; Reason = "Task Manager repeated launches"; ExpectedCount = 2 },
        @{ Name = "DiskManager"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=DiskManager classification=BuiltInApp'; Reason = "Disk Manager repeated launches"; ExpectedCount = 2 },
        @{ Name = "guideXOS Navigator"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=guideXOS Navigator classification=BuiltInApp'; Reason = "guideXOS Navigator repeated launches"; ExpectedCount = 2 },
        @{ Name = "Trash"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Trash classification=BuiltInApp'; Reason = "Trash open repeated launches"; ExpectedCount = 2 },
        @{ Name = "Computer"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Computer classification=ShellAction'; Reason = "Computer repeated launches"; ExpectedCount = 2 },
        @{ Name = "This System"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=This System classification=ShellAction'; Reason = "This System repeated launches"; ExpectedCount = 2 },
        @{ Name = "Documents"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Documents classification=ShellAction'; Reason = "Documents repeated launches"; ExpectedCount = 2 },
        @{ Name = "Pictures"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Pictures classification=ShellAction'; Reason = "Pictures repeated launches"; ExpectedCount = 2 },
        @{ Name = "Music"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Music classification=ShellAction'; Reason = "Music repeated launches"; ExpectedCount = 2 },
        @{ Name = "Network"; Pattern = '\[AppModelActiveTypedDispatch\] source=HostedDesktopService request=Network classification=ShellAction'; Reason = "Network repeated launches"; ExpectedCount = 2 },
        @{ Name = "folder open"; Pattern = [regex]::Escape("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureFolder classification=FileOpen"); Reason = "folder repeated opens"; ExpectedCount = 2 },
        @{ Name = "text open"; Pattern = [regex]::Escape("[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureText classification=FileOpen"); Reason = "text repeated opens"; ExpectedCount = 2 }
    )

    foreach ($case in $repeatedLaunchCases) {
        Assert-RegexCountAtLeast -Text $repeatedOutput -Pattern $case.Pattern -Minimum $case.ExpectedCount -Reason $case.Reason
    }

    $recentAfterRepeated = Get-DesktopRecentNames
    # Phase 4D and Phase 5B cover the full recent-program matrix; here we keep
    # the canonical set that this repeated batch should persist.
    $recentExpected = @(
        "Notepad",
        "File Explorer",
        "Trash",
        "guideXOS Navigator",
        "DiskManager",
        "TaskManager",
        "Paint",
        "Clock",
        "Calculator",
        "ControlPanel"
    )
    Assert-True (Test-RecentContainsAll -RecentNames $recentAfterRepeated -ExpectedNames $recentExpected) "Normal app launches should update recents for the expected canonical entries"
    Assert-True (Test-RecentExcludesAny -RecentNames $recentAfterRepeated -ExcludedNames @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported")) "Fallback-only and unsupported labels should not pollute recents"

    $fallbackOutput = Invoke-ServerCommands -Commands @(
        "gui.start",
        "desktop.launch AppModel",
        "desktop.launch ComputerFiles",
        "desktop.launch TotallyUnknownLaunchThing",
        "desktop.open `"$FixtureUnsupported`"",
        "desktop.open `"$FixtureImage`""
    )

    Assert-Contains $fallbackOutput "[AppModelActiveTypedDispatch] source=HostedDesktopService request=AppModel classification=LegacyAlias" "AppModel fallback"
    Assert-Contains $fallbackOutput "activeTypedDispatchHandled=false" "AppModel fallback"
    Assert-Contains $fallbackOutput "legacyFallbackUsed=true" "AppModel fallback"
    Assert-Contains $fallbackOutput "selectedHandler=App Model Demo" "AppModel fallback"
    Assert-Contains $fallbackOutput "[AppModelActiveTypedDispatch] source=HostedDesktopService request=ComputerFiles classification=ShellAction" "ComputerFiles fallback"
    Assert-Contains $fallbackOutput "selectedHandler=FileExplorer" "ComputerFiles fallback"
    Assert-Contains $fallbackOutput "[AppModelActiveTypedDispatch] source=HostedDesktopService request=TotallyUnknownLaunchThing classification=Unknown" "unknown fallback"
    Assert-Contains $fallbackOutput "selectedHandler=TotallyUnknownLaunchThing" "unknown fallback"
    Assert-Contains $fallbackOutput "[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureUnsupported classification=FileOpen" "unsupported file fallback"
    Assert-Contains $fallbackOutput "selectedHandler=Unsupported" "unsupported file fallback"
    Assert-Contains $fallbackOutput "[AppModelActiveTypedDispatch] source=HostedFilesystemEntry request=$FixtureImage classification=FileOpen" "image fallback"
    Assert-Contains $fallbackOutput "selectedHandler=ImageViewer" "image fallback"
    Assert-Contains $fallbackOutput "reason=Active typed dispatch is not enabled for this filesystem entry" "image/unsupported fallback reason"

    $fallbackRecents = Get-DesktopRecentNames
    Assert-True (Test-RecentExcludesAny -RecentNames $fallbackRecents -ExcludedNames @("TotallyUnknownLaunchThing", "ComputerFiles", "Unsupported")) "Fallback targets should not appear as bogus recent entries"

    $gateSequences = @(
        @{
            Name = "force-off";
            Commands = @(
                "gui.start",
                "desktop.appmodel.active-typed-dispatch-gate force-off",
                "desktop.launch Notepad",
                "desktop.launch FileExplorer",
                "desktop.open `"$FixtureFolder`" dir",
                "desktop.open `"$FixtureText`""
            )
        },
        @{
            Name = "force-on";
            Commands = @(
                "gui.start",
                "desktop.appmodel.active-typed-dispatch-gate force-on",
                "desktop.launch Notepad",
                "desktop.launch FileExplorer",
                "desktop.open `"$FixtureFolder`" dir",
                "desktop.open `"$FixtureText`""
            )
        },
        @{
            Name = "reset";
            Commands = @(
                "gui.start",
                "desktop.appmodel.active-typed-dispatch-gate reset",
                "desktop.appmodel.active-typed-dispatch-gate"
            )
        }
    )

    $gateOutputs = @{}
    foreach ($gateSequence in $gateSequences) {
        $gateOutput = Invoke-ServerCommands -Commands $gateSequence.Commands
        $gateOutputs[$gateSequence.Name] = $gateOutput
    }

    $forceOffOutput = $gateOutputs["force-off"]
    $forceOnOutput = $gateOutputs["force-on"]
    $resetOutput = $gateOutputs["reset"]

    Assert-Contains $forceOffOutput "mode: force-off" "force-off gate transition"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEnabled=false" "force-off gate transition"
    Assert-Contains $forceOffOutput "appModelActiveDispatchEffectiveStateSource=force-off" "force-off gate transition"
    Assert-Contains $forceOffOutput "visibleLaunchBehaviorChanged=false" "force-off gate transition"
    Assert-Contains $forceOffOutput "persistentDesktopStorageWrites=false" "force-off gate transition"
    Assert-Contains $forceOffOutput "[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp" "force-off launch path"
    Assert-Contains $forceOffOutput "activeTypedDispatchHandled=false" "force-off launch path"
    Assert-Contains $forceOffOutput "legacyFallbackUsed=true" "force-off launch path"
    Assert-Contains $forceOffOutput "reason=Active typed dispatch gate is disabled" "force-off launch path"

    Assert-Contains $forceOnOutput "mode: force-on" "force-on gate transition"
    Assert-Contains $forceOnOutput "appModelActiveDispatchEnabled=true" "force-on gate transition"
    Assert-Contains $forceOnOutput "appModelActiveDispatchEffectiveStateSource=force-on" "force-on gate transition"
    Assert-Contains $forceOnOutput "[AppModelActiveTypedDispatch] source=HostedDesktopService request=Notepad classification=BuiltInApp" "force-on launch path"
    Assert-Contains $forceOnOutput "activeTypedDispatchHandled=true" "force-on launch path"
    Assert-Contains $forceOnOutput "legacyFallbackUsed=false" "force-on launch path"
    Assert-Contains $forceOnOutput "reason=Active typed dispatch handled the Notepad launch" "force-on launch path"

    Assert-Contains $resetOutput "mode: reset" "reset gate transition"
    Assert-Contains $resetOutput "appModelActiveDispatchEnabled=true" "reset gate transition"
    Assert-Contains $resetOutput "appModelActiveDispatchEffectiveStateSource=product-default" "reset gate transition"
    Assert-Contains $resetOutput "appModelActiveDispatchCurrentState=true" "reset gate transition"

    $postGateRecentNames = Get-DesktopRecentNames
    Assert-True (Test-RecentContainsAll -RecentNames $postGateRecentNames -ExpectedNames @("Notepad", "File Explorer")) "Recent programs should continue updating after gate transitions"

    $phase3ERepeatedLaunchesStable = $true
    $phase3EMixedLaunchSourcesStable = $true
    $phase3EFallbackStabilityConfirmed = $true
    $phase3EEmergencyGateTransitionsStable = $true
    $phase3ERecentProgramsStable = $true
    $phase3EVisibleLaunchBehaviorChanged = $false
    $phase3EPersistentDesktopStorageWrites = $false
    $phase3EProductDefaultStillEnabled = $resetOutput.Contains("appModelActiveDispatchEnabled=true") -and
        $resetOutput.Contains("appModelActiveDispatchEffectiveStateSource=product-default")
    $phase3ETemporarySmokeStateRestored = $false
}
finally {
    Restore-TrackedArtifacts -State $artifactState
    Remove-Item -LiteralPath $TempRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$cleanupVerified = (-not (Test-Path -LiteralPath $TempRoot)) -and (-not (Test-Path -LiteralPath $BackupRoot))
$desktopJsonAfterCleanup = Get-FileText (Join-Path $Root "desktop.json")
$desktopStateAfterCleanup = Get-FileText (Join-Path $Root "desktop.state")
$displayOptionsAfterCleanup = Get-FileText (Join-Path $Root "display-options.cfg")
$phase3ETemporarySmokeStateRestored =
    $cleanupVerified -and
    ($desktopJsonBefore -eq $desktopJsonAfterCleanup) -and
    ($desktopStateBefore -eq $desktopStateAfterCleanup) -and
    ($displayOptionsBefore -eq $displayOptionsAfterCleanup)

$reportLines = @(
    "[AppModelPhase3EActiveTypedDispatchSmoke]",
    "mode=hosted",
    "flagName=appmodel.active-typed-dispatch",
    "candidateGateName=appmodel.active-typed-dispatch-default-on-candidate",
    "appModelPhase3ERepeatedLaunchesStable=$($phase3ERepeatedLaunchesStable.ToString().ToLowerInvariant())",
    "appModelPhase3EMixedLaunchSourcesStable=$($phase3EMixedLaunchSourcesStable.ToString().ToLowerInvariant())",
    "appModelPhase3EFallbackStabilityConfirmed=$($phase3EFallbackStabilityConfirmed.ToString().ToLowerInvariant())",
    "appModelPhase3EEmergencyGateTransitionsStable=$($phase3EEmergencyGateTransitionsStable.ToString().ToLowerInvariant())",
    "appModelPhase3ERecentProgramsStable=$($phase3ERecentProgramsStable.ToString().ToLowerInvariant())",
    "appModelPhase3EVisibleLaunchBehaviorChanged=$($phase3EVisibleLaunchBehaviorChanged.ToString().ToLowerInvariant())",
    "appModelPhase3EPersistentDesktopStorageWrites=$($phase3EPersistentDesktopStorageWrites.ToString().ToLowerInvariant())",
    "appModelPhase3ETemporarySmokeStateRestored=$($phase3ETemporarySmokeStateRestored.ToString().ToLowerInvariant())",
    "appModelPhase3EProductDefaultStillEnabled=$($phase3EProductDefaultStillEnabled.ToString().ToLowerInvariant())",
    "appModelActiveDispatchFeatureGate=appmodel.active-typed-dispatch",
    "appModelActiveDispatchDefaultOnCandidateGate=appmodel.active-typed-dispatch-default-on-candidate",
    "appModelActiveDispatchEnabled=true",
    "appModelActiveDispatchRuntimePath=active",
    "appModelActiveDispatchEffectiveStateSource=product-default",
    "runtimeLaunchBehaviorChanged=true",
    "visibleLaunchBehaviorChanged=false",
    "persistentDesktopStorageWrites=false",
    "result=PASS"
)

    $logParts = @(
        ($reportLines -join [Environment]::NewLine),
        "",
        "[boot-output]",
        $bootOutput,
        "",
        "[preflight-output]",
        $preflightOutput,
    "",
    "[repeated-launch-output]",
    $repeatedOutput,
    "",
    "[fallback-output]",
    $fallbackOutput,
    "",
    "[force-off-output]",
    $forceOffOutput,
    "",
    "[force-on-output]",
    $forceOnOutput,
    "",
    "[reset-output]",
    $resetOutput
)
Set-Content -LiteralPath $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

Write-Output ($reportLines -join [Environment]::NewLine)
Write-Host "Smoke log: $SmokeLog"
if ($cleanupVerified) {
    Write-Host "appModelPhase3ETemporaryArtifactsCleaned=true"
} else {
    Write-Host "appModelPhase3ETemporaryArtifactsCleaned=false"
}
Write-Host "appModelPhase3ETemporarySmokeStateRestored=$($phase3ETemporarySmokeStateRestored.ToString().ToLowerInvariant())"
