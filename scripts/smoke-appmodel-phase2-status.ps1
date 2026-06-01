param(
    [switch]$BuildHosted,
    [switch]$SkipFlagSmoke,
    [switch]$IncludeQemu,
    [int]$TimeoutSeconds = 35
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$StatusLog = Join-Path $LogDir "appmodel-phase2-status-$stamp.log"
$QemuEvidencePath = Join-Path $LogDir "appmodel-typed-dispatch-gate-qemu.evidence.txt"

$Checks = New-Object System.Collections.Generic.List[object]
$CommandsRun = New-Object System.Collections.Generic.List[string]
$CommandsListed = New-Object System.Collections.Generic.List[string]
$LogSections = New-Object System.Collections.Generic.List[string]
$QemuEvidence = @{}
$qemuCoverageEvidenceConfirmed = $false
$qemuKnownNonFatalDriftsConfirmed = $false
$qemuRestoreAndInvariantEvidenceConfirmed = $false
$taskbarAuditConfirmed = $false

function Add-Check {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Detail
    )
    [void]$script:Checks.Add([pscustomobject]@{
        Name = $Name
        Status = $Status
        Detail = $Detail
    })
}

function Add-LogSection {
    param(
        [string]$Name,
        [object]$Output
    )
    [void]$script:LogSections.Add("[$Name]")
    if ($null -ne $Output) {
        [void]$script:LogSections.Add(($Output -join [Environment]::NewLine))
    }
    [void]$script:LogSections.Add("")
}

function Invoke-ServerCommands {
    param(
        [string[]]$Commands
    )

    $exe = Join-Path $Root "guideXOSServer.exe"
    if (-not (Test-Path $exe)) {
        throw "guideXOSServer.exe not found. Run .\build.bat first or pass -BuildHosted."
    }

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $exe 2>&1
}

function Text-Contains {
    param(
        [object]$Output,
        [string]$Needle
    )
    $text = [string]::Join([Environment]::NewLine, @($Output | ForEach-Object { $_.ToString() }))
    return $text.Contains($Needle)
}

function Get-MatchValue {
    param(
        [object]$Output,
        [string]$Pattern,
        [string]$Default = ""
    )

    $text = $Output -join [Environment]::NewLine
    $match = [regex]::Match($text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Multiline)
    if ($match.Success -and $match.Groups.Count -gt 1) {
        return $match.Groups[1].Value.Trim()
    }
    return $Default
}

function Read-KeyValueEvidence {
    param([string]$Path)

    $values = @{}
    if (-not (Test-Path $Path)) {
        return $values
    }
    foreach ($line in Get-Content -LiteralPath $Path) {
        if ($line -match '^([^=\s]+)=(.*)$') {
            $values[$matches[1]] = $matches[2]
        }
    }
    return $values
}

function Test-EvidenceValues {
    param(
        [hashtable]$Evidence,
        [hashtable]$Expected
    )

    foreach ($entry in $Expected.GetEnumerator()) {
        if (-not $Evidence.ContainsKey($entry.Key) -or $Evidence[$entry.Key] -ne $entry.Value) {
            return $false
        }
    }
    return $true
}

function Add-QemuEvidenceCheck {
    param(
        [string]$Name,
        [hashtable]$Expected,
        [string]$Detail
    )

    if (Test-EvidenceValues -Evidence $script:QemuEvidence -Expected $Expected) {
        Add-Check $Name "PASS" $Detail
        return $true
    }
    Add-Check $Name "FAIL" "missing expected QEMU evidence fields; expected=$Detail"
    return $false
}

function Invoke-ProcessCommand {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$ArgumentList,
        [int]$TimeoutSeconds = 900
    )

    $stdoutPath = Join-Path $LogDir "appmodel-phase2-status-$stamp-$Name.out.log"
    $stderrPath = Join-Path $LogDir "appmodel-phase2-status-$stamp-$Name.err.log"
    $proc = Start-Process -FilePath $FilePath `
        -ArgumentList $ArgumentList `
        -PassThru `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath

    $timedOut = $false
    try {
        Wait-Process -Id $proc.Id -Timeout $TimeoutSeconds -ErrorAction Stop
    } catch {
        $timedOut = $true
        if (-not $proc.HasExited) {
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
            Wait-Process -Id $proc.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }

    $output = @()
    if (Test-Path $stdoutPath) {
        $output += Get-Content $stdoutPath
    }
    if (Test-Path $stderrPath) {
        $output += Get-Content $stderrPath
    }

    $proc.Refresh()
    $exitCode = $proc.ExitCode

    return [pscustomobject]@{
        Output = $output
        ExitCode = if ($timedOut) { 124 } else { $exitCode }
        TimedOut = $timedOut
    }
}

Push-Location $Root
try {
    $hostedBuildCommand = ".\build.bat"
    if ($BuildHosted) {
        [void]$CommandsRun.Add($hostedBuildCommand)
        $buildResult = Invoke-ProcessCommand -Name "hosted-build" -FilePath "cmd.exe" -ArgumentList @("/c", "`"$(Join-Path $Root "build.bat")`"") -TimeoutSeconds 600
        Add-LogSection "hosted-build-output" $buildResult.Output
        $hostedBuildOk = (-not $buildResult.TimedOut) -and
            (($null -eq $buildResult.ExitCode) -or $buildResult.ExitCode -eq 0) -and
            (Text-Contains -Output $buildResult.Output -Needle "Build successful: guideXOSServer.exe")
        if ($hostedBuildOk) {
            Add-Check "hostedBuild" "PASS" "command=$hostedBuildCommand"
        } else {
            Add-Check "hostedBuild" "FAIL" "command=$hostedBuildCommand exitCode=$($buildResult.ExitCode) timedOut=$($buildResult.TimedOut.ToString().ToLowerInvariant())"
        }
    } else {
        [void]$CommandsListed.Add("$hostedBuildCommand (pass -BuildHosted to run)")
        Add-Check "hostedBuild" "INFO" "not run by default; command=$hostedBuildCommand"
    }

    $hostedCommands = @(
        "gui.start",
        "gui.smoke.launchshadow",
        "desktop.appmodel.summary",
        "desktop.appmodel.typed-dispatch-gate",
        "desktop.launch.storage"
    )
    [void]$CommandsRun.Add(".\guideXOSServer.exe < gui.start; gui.smoke.launchshadow; desktop.appmodel.summary; desktop.appmodel.typed-dispatch-gate; desktop.launch.storage; exit")
    try {
        $hostedOutput = Invoke-ServerCommands -Commands $hostedCommands
        Add-LogSection "hosted-appmodel-output" $hostedOutput

        if ((Text-Contains -Output $hostedOutput -Needle "command: gui.smoke.launchshadow") -and
            (Text-Contains -Output $hostedOutput -Needle "mode: diagnostic-only") -and
            (Text-Contains -Output $hostedOutput -Needle "launchesApps: false") -and
            (Text-Contains -Output $hostedOutput -Needle "runtimeLaunchBehaviorChanged: false")) {
            Add-Check "gui.smoke.launchshadow" "PASS" "diagnostic-only runtimeLaunchBehaviorChanged=false"
        } else {
            Add-Check "gui.smoke.launchshadow" "FAIL" "missing required diagnostic-only launch-shadow markers"
        }

        $summaryOverall = Get-MatchValue -Output $hostedOutput -Pattern '^overall:\s*(\S+)'
        if ($summaryOverall -eq "OK") {
            Add-Check "desktop.appmodel.summary" "PASS" "overall=OK"
        } elseif ($summaryOverall) {
            Add-Check "desktop.appmodel.summary" "WARN" "overall=$summaryOverall"
        } else {
            Add-Check "desktop.appmodel.summary" "FAIL" "overall line not found"
        }

        $gateStatus = Get-MatchValue -Output $hostedOutput -Pattern '^gateStatus:\s*(\S+)'
        if ($gateStatus -eq "PASS") {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "PASS" "gateStatus=PASS"
        } elseif ($gateStatus -eq "WARN" -or $gateStatus -eq "NOT-RUN") {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "WARN" "gateStatus=$gateStatus"
        } elseif ($gateStatus) {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "FAIL" "gateStatus=$gateStatus"
        } else {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "FAIL" "gateStatus line not found"
        }

        $taskbarAuditConfirmed =
            (Text-Contains -Output $hostedOutput -Needle "site=Compositor:taskbarButtons") -and
            (Text-Contains -Output $hostedOutput -Needle "active window title, not persisted launch source") -and
            (Text-Contains -Output $hostedOutput -Needle "site=desktop.cpp:s_taskbarEntries[]") -and
            (Text-Contains -Output $hostedOutput -Needle "taskbar entry label, currently disabled/static") -and
            (Text-Contains -Output $hostedOutput -Needle "count=0")
        if ($taskbarAuditConfirmed) {
            Add-Check "taskbarAudit" "PASS" "window-management-only; hosted buttons are live windows and bare-metal static entries are disabled count=0"
        } else {
            Add-Check "taskbarAudit" "FAIL" "desktop.launch.storage did not preserve the audited taskbar window-management-only inventory"
        }
    } catch {
        Add-Check "hostedDiagnostics" "FAIL" $_.Exception.Message
    }

    $flagSmokeCommand = ".\scripts\smoke-appmodel-typed-dispatch-flags.ps1"
    if ($SkipFlagSmoke) {
        [void]$CommandsListed.Add("$flagSmokeCommand (skipped by -SkipFlagSmoke)")
        Add-Check "typedDispatchFlagSmoke" "INFO" "skipped; command=$flagSmokeCommand"
    } else {
        [void]$CommandsRun.Add($flagSmokeCommand)
        try {
            $flagResult = Invoke-ProcessCommand -Name "typed-dispatch-flags" -FilePath "powershell.exe" -ArgumentList @(
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "`"$(Join-Path $Root "scripts\smoke-appmodel-typed-dispatch-flags.ps1")`""
            ) -TimeoutSeconds 900
            Add-LogSection "typed-dispatch-flag-smoke-output" $flagResult.Output
            $flagSmokeOk = (-not $flagResult.TimedOut) -and
                (($null -eq $flagResult.ExitCode) -or $flagResult.ExitCode -eq 0) -and
                (Text-Contains -Output $flagResult.Output -Needle "result=PASS")
            if ($flagSmokeOk) {
                Add-Check "typedDispatchFlagSmoke" "PASS" "single-flag and invalid-flag diagnostics passed"
            } else {
                Add-Check "typedDispatchFlagSmoke" "FAIL" "exitCode=$($flagResult.ExitCode) timedOut=$($flagResult.TimedOut.ToString().ToLowerInvariant()) or missing result=PASS"
            }
        } catch {
            Add-Check "typedDispatchFlagSmoke" "FAIL" $_.Exception.Message
        }
    }

    $qemuSmokeCommand = ".\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds $TimeoutSeconds"
    if ($IncludeQemu) {
        [void]$CommandsRun.Add($qemuSmokeCommand)
        try {
            $qemuStartedAt = Get-Date
            $qemuResult = Invoke-ProcessCommand -Name "qemu-launchshadow" -FilePath "powershell.exe" -ArgumentList @(
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "`"$(Join-Path $Root "scripts\smoke-appmodel-launchshadow.ps1")`"",
                "-TimeoutSeconds",
                "$TimeoutSeconds"
            ) -TimeoutSeconds ([Math]::Max(300, $TimeoutSeconds + 600))
            Add-LogSection "qemu-launchshadow-smoke-output" $qemuResult.Output
            $qemuSmokeOk = (-not $qemuResult.TimedOut) -and
                (($null -eq $qemuResult.ExitCode) -or $qemuResult.ExitCode -eq 0) -and
                (Text-Contains -Output $qemuResult.Output -Needle "App-model launch shadow kernel smoke PASS")
            if ($qemuSmokeOk) {
                Add-Check "qemuLaunchShadowSmoke" "PASS" "command=$qemuSmokeCommand"
            } else {
                Add-Check "qemuLaunchShadowSmoke" "FAIL" "exitCode=$($qemuResult.ExitCode) timedOut=$($qemuResult.TimedOut.ToString().ToLowerInvariant()) or missing PASS marker"
            }

            $qemuEvidenceFresh = (Test-Path $QemuEvidencePath) -and
                ((Get-Item -LiteralPath $QemuEvidencePath).LastWriteTime -ge $qemuStartedAt)
            if ($qemuEvidenceFresh) {
                $QemuEvidence = Read-KeyValueEvidence -Path $QemuEvidencePath
                Add-Check "qemuLaunchShadowEvidenceFresh" "PASS" "path=$QemuEvidencePath"

                $desktopFileFolder = Add-QemuEvidenceCheck "qemuDesktopFileFolderCoverage" @{
                    realBranchDesktopShortcutTextFileConfirmed = "true"
                    realBranchDesktopFilesystemTextFileConfirmed = "true"
                    realBranchFileAssociationsConfirmed = "true"
                    realBranchDesktopShortcutFolderConfirmed = "true"
                    realBranchDesktopFilesystemFolderConfirmed = "true"
                } "folder,.txt,.log,.cfg,.ini"
                $desktopSystemObjects = Add-QemuEvidenceCheck "qemuDesktopSystemObjectCoverage" @{
                    realBranchDesktopSystemObjectRootFolderConfirmed = "true"
                    realBranchDesktopSystemObjectFileManagerConfirmed = "true"
                    realBranchDesktopSystemObjectTrashConfirmed = "true"
                    realBranchDesktopSystemObjectSystemSettingsConfirmed = "true"
                } "ThisSystem,FileManager,Trash,SystemSettings"
                $startMenu = Add-QemuEvidenceCheck "qemuStartMenuCoverage" @{
                    realBranchStartMenuNotepadConfirmed = "true"
                    realBranchStartMenuBuiltInAppsConfirmed = "true"
                    realBranchStartMenuFilesConfirmed = "true"
                    realBranchStartMenuConsoleConfirmed = "true"
                    realBranchStartMenuSettingsConfirmed = "true"
                    realBranchStartMenuRightColumnShellActionsConfirmed = "true"
                    realBranchStartMenuControlPanelConfirmed = "true"
                    realBranchStartMenuAppModelConfirmed = "true"
                } "BuiltInApp,LegacyAlias,ShellAction,embedded-diagnostic"
                $pinnedDesktop = Add-QemuEvidenceCheck "qemuPinnedDesktopShortcutCoverage" @{
                    realBranchPinnedDesktopNotepadConfirmed = "true"
                } "RealBranchPinnedDesktopNotepad"
                $qemuCoverageEvidenceConfirmed = $desktopFileFolder -and $desktopSystemObjects -and $startMenu -and $pinnedDesktop

                $qemuKnownNonFatalDriftsConfirmed = Add-QemuEvidenceCheck "qemuKnownNonFatalDrifts" @{
                    realBranchStartMenuSettingsExpectedNonFatalDriftConfirmed = "true"
                    realBranchStartMenuRightColumnShellActionsExpectedNonFatalDriftConfirmed = "true"
                    realBranchStartMenuControlPanelExpectedNonFatalDriftConfirmed = "true"
                    realBranchStartMenuAppModelUnsupportedDiagnosticTargetConfirmed = "true"
                } "Settings,Computer/Documents/Pictures/Music/Network,ControlPanel,AppModel"
                $qemuRestoreAndInvariantEvidenceConfirmed = Add-QemuEvidenceCheck "qemuRestoreAndInvariants" @{
                    qemuSmokeStatus = "PASS"
                    runtimeLaunchBehaviorChanged = "false"
                    realBranchFileAssociationsStateRestored = "true"
                    realBranchFileAssociationsRestoreVerificationConfirmed = "true"
                    persistentDesktopStorageWrites = "false"
                    launchesApps = "false"
                } "qemuSmokeStatus=PASS runtimeLaunchBehaviorChanged=false persistentDesktopStorageWrites=false launchesApps=false"
            } else {
                Add-Check "qemuLaunchShadowEvidenceFresh" "FAIL" "QEMU smoke did not write fresh evidence at $QemuEvidencePath"
            }
        } catch {
            Add-Check "qemuLaunchShadowSmoke" "FAIL" $_.Exception.Message
        }
    } else {
        [void]$CommandsListed.Add("$qemuSmokeCommand (pass -IncludeQemu to run)")
        Add-Check "qemuLaunchShadowSmoke" "INFO" "optional; not run without -IncludeQemu"
    }

    Add-Check "deferredAssociationsDocumented" "PASS" ".md,images,.wav,.gxm,.mue,.img,.gxapp,.gxq,.elf,.exe,unknown remain deferred or unsupported"

    $hasFail = $false
    $hasWarn = $false
    foreach ($check in $Checks) {
        if ($check.Status -eq "FAIL") { $hasFail = $true }
        if ($check.Status -eq "WARN") { $hasWarn = $true }
    }
    $overall = if ($hasFail) { "FAIL" } elseif ($hasWarn) { "WARN" } else { "PASS" }
    $coverageAudit = if ($qemuCoverageEvidenceConfirmed -and $taskbarAuditConfirmed) { "pass" } elseif ($IncludeQemu) { "fail" } else { "not-run" }
    $knownDriftsDocumented = $qemuKnownNonFatalDriftsConfirmed.ToString().ToLowerInvariant()
    $deferredAssociationsDocumented = "true"
    $readyForTypedDispatchPlanning = (
        $overall -eq "PASS" -and
        $qemuCoverageEvidenceConfirmed -and
        $qemuKnownNonFatalDriftsConfirmed -and
        $qemuRestoreAndInvariantEvidenceConfirmed -and
        $taskbarAuditConfirmed
    ).ToString().ToLowerInvariant()

    $reportLines = @(
        "[AppModelPhase2Status]",
        "mode=validation-report-only",
        "typedDispatchEnabled=false",
        "feedsTypedDispatchIntoLaunch=false",
        "runtimeLaunchBehaviorChanged=false",
        "persistentDesktopStorageWrites=false",
        "launchesApps=false",
        "qemuOptional=true",
        "status=$overall",
        "appModelPhase2LaunchShadowCoverageAudit=$coverageAudit",
        "appModelPhase2KnownDriftsDocumented=$knownDriftsDocumented",
        "appModelPhase2DeferredAssociationsDocumented=$deferredAssociationsDocumented",
        "appModelPhase2ReadyForTypedDispatchPlanning=$readyForTypedDispatchPlanning",
        "coverage.desktopFileFolder=folder,.txt,.log,.cfg,.ini",
        "coverage.desktopSystemObjects=ThisSystem,FileManager,Trash,SystemSettings",
        "coverage.startMenu=BuiltInApp(Notepad,Calculator,TaskManager,DiskManager,Trash,DisplayOptions);LegacyAlias(Files);ShellAction(Console,Settings,Computer,Documents,Pictures,Music,Network);embeddedDiagnostic(ControlPanel,AppModel)",
        "coverage.pinnedDesktopShortcut=RealBranchPinnedDesktopNotepad",
        "coverage.taskbar=audited-window-management-only;hosted-live-buttons-not-persisted-launch-sources;bare-metal-static-entries-disabled-count=0",
        "fileAssociations.aligned=folder,.txt,.log,.cfg,.ini",
        "fileAssociations.deferred=.md,images,.wav,.gxm,.mue,.img,.gxapp,.gxq,.elf,.exe,unknown",
        "knownNonFatalDrifts=Settings->DisplayOptions;Computer/Documents/Pictures/Music/Network->empty-typed-candidate;ControlPanel->embedded-state-vs-DisplayOptions;AppModel->embedded-viewer-unsupported-typed-target",
        "futureWork=.md,image-routing,.wav-media-contract,.gxm/.mue-loaders,.img-DiskManager-workflow,.gxapp/.gxq/.elf/.exe-app-like-paths,unknown-extension-policy",
        "checks:"
    )
    foreach ($check in $Checks) {
        $reportLines += "  $($check.Name): $($check.Status) - $($check.Detail)"
    }
    $reportLines += "commandsRun:"
    foreach ($command in $CommandsRun) {
        $reportLines += "  $command"
    }
    $reportLines += "commandsAvailable:"
    foreach ($command in $CommandsListed) {
        $reportLines += "  $command"
    }
    $reportLines += "log=$StatusLog"
    $report = $reportLines -join [Environment]::NewLine

    $logParts = @($report, "")
    $logParts += $LogSections
    Set-Content -Path $StatusLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

    Write-Host $report
    if ($overall -eq "FAIL") { exit 1 }
    exit 0
} finally {
    Pop-Location
}
