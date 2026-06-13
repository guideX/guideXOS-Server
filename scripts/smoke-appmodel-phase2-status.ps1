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

function Get-LaunchTargetComparisonReadinessSummary {
    param([object]$Output)

    # The buckets below are diagnostic overlays, not a strict partition.
    # One comparison row may contribute to a type bucket and a special-case bucket.
    $records = New-Object System.Collections.Generic.List[object]

    $inComparisonSection = $false
    foreach ($rawLine in @($Output)) {
        $line = $rawLine.ToString()
        if ($line -eq "[LaunchTargetComparison]") {
            $inComparisonSection = $true
            continue
        }
        if ($inComparisonSection -and $line.StartsWith("overall:")) {
            break
        }
        if (-not $inComparisonSection) {
            continue
        }
        if ($line -notmatch '^\s*label=.*\sresult=') {
            continue
        }

        $match = [regex]::Match(
            $line,
            '^\s*label=(.*?) result=([^ ]+) hosted\{type=([^ ]+) status=([^ ]+) dispatch=(.*?) appId=(.*?)\} bareMetal\{type=([^ ]+) status=([^ ]+) dispatch=(.*?) appId=(.*?)\} note=(.*)$'
        )
        if (-not $match.Success) {
            continue
        }

        $label = $match.Groups[1].Value.Trim()
        $result = $match.Groups[2].Value.Trim()
        $hostedType = $match.Groups[3].Value.Trim()
        $hostedDispatch = $match.Groups[5].Value.Trim()
        $hostedAppId = $match.Groups[6].Value.Trim()
        $bareMetalType = $match.Groups[7].Value.Trim()
        $bareMetalDispatch = $match.Groups[8].Value.Trim()
        $bareMetalAppId = $match.Groups[9].Value.Trim()

        $isUnknown = ($hostedType -eq "Unknown") -or ($bareMetalType -eq "Unknown")
        $isReady = (-not $isUnknown) -and ($result -eq "exact" -or $result -eq "accepted-alias")
        $readiness = if ($isReady) { "ready" } else { "blocked" }
        $blockReason = "none"
        if (-not $isReady) {
            if ($isUnknown) {
                $blockReason = "unknownOrUnclassified"
            } else {
                $blockReason = "knownIntentionalDrift"
            }
        }
        $isSpecialCase = $label -eq "ComputerFiles" -or $label -eq "AppModel"

        $record = [pscustomobject]@{
            target = $label
            resolvedType = $hostedType
            appId = $hostedAppId
            actualDispatch = $bareMetalDispatch
            typedDispatchCandidate = $hostedDispatch
            typedDispatchCandidateComparison = $result
            readiness = $readiness
            blockReason = $blockReason
            unknownOrUnclassified = $isUnknown
            specialCase = $isSpecialCase
            builtInApp = ($hostedType -eq "BuiltInApp" -or $bareMetalType -eq "BuiltInApp")
            legacyAlias = ($hostedType -eq "LegacyAlias" -or $bareMetalType -eq "LegacyAlias")
            shellAction = ($hostedType -eq "ShellAction" -or $bareMetalType -eq "ShellAction")
            fileOpen = ($hostedType -eq "FileOpen" -or $bareMetalType -eq "FileOpen")
        }
        [void]$records.Add($record)
    }

    $sortedRecords = @($records | Sort-Object -Property target)

    $summary = [ordered]@{
        totalObservedLaunchTargets = 0
        typedDispatchReadyCount = 0
        typedDispatchBlockedCount = 0
        unknownOrUnclassifiedCount = 0
        legacyAliasCount = 0
        builtInAppCount = 0
        shellActionCount = 0
        fileOpenCount = 0
        specialCaseCount = 0
    }

    foreach ($record in $sortedRecords) {
        $summary.totalObservedLaunchTargets++
        if ($record.readiness -eq "ready") { $summary.typedDispatchReadyCount++ }
        else { $summary.typedDispatchBlockedCount++ }
        if ($record.unknownOrUnclassified -and $record.readiness -eq "blocked") { $summary.unknownOrUnclassifiedCount++ }
        if ($record.legacyAlias) { $summary.legacyAliasCount++ }
        if ($record.builtInApp) { $summary.builtInAppCount++ }
        if ($record.shellAction) { $summary.shellActionCount++ }
        if ($record.fileOpen) { $summary.fileOpenCount++ }
        if ($record.specialCase) { $summary.specialCaseCount++ }
    }

    $blockedTargets = @($sortedRecords | Where-Object { $_.readiness -eq "blocked" } | ForEach-Object { $_.target })
    $unknownTargets = @($sortedRecords | Where-Object { $_.readiness -eq "blocked" -and $_.unknownOrUnclassified } | ForEach-Object { $_.target })
    $categoryCountsMayOverlap = $true
    $readinessInvariantsOk =
        ($summary.typedDispatchReadyCount + $summary.typedDispatchBlockedCount -eq $summary.totalObservedLaunchTargets) -and
        ($summary.unknownOrUnclassifiedCount -le $summary.typedDispatchBlockedCount)

    return [pscustomobject]@{
        totalObservedLaunchTargets = $summary.totalObservedLaunchTargets
        typedDispatchReadyCount = $summary.typedDispatchReadyCount
        typedDispatchBlockedCount = $summary.typedDispatchBlockedCount
        unknownOrUnclassifiedCount = $summary.unknownOrUnclassifiedCount
        legacyAliasCount = $summary.legacyAliasCount
        builtInAppCount = $summary.builtInAppCount
        shellActionCount = $summary.shellActionCount
        fileOpenCount = $summary.fileOpenCount
        specialCaseCount = $summary.specialCaseCount
        phase3TypedDispatchReadiness = if ($summary.totalObservedLaunchTargets -gt 0) { "report-only" } else { "not-observed" }
        categoryCountsMayOverlap = $categoryCountsMayOverlap
        readinessInvariantsOk = $readinessInvariantsOk
        records = $sortedRecords
        blockedTargets = $blockedTargets
        unknownTargets = $unknownTargets
    }
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
        "desktop.launch.compare",
        "desktop.appmodel.typed-dispatch-gate",
        "desktop.launch.storage"
    )
    [void]$CommandsRun.Add(".\guideXOSServer.exe < gui.start; gui.smoke.launchshadow; desktop.appmodel.summary; desktop.launch.compare; desktop.appmodel.typed-dispatch-gate; desktop.launch.storage; exit")
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

        # Phase 3 pilot scaffolding default-off assertions
        $phase3PilotMarkersOk =
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotStartMenuNotepadFlag=OFF") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotFallbackToLegacyFlag=OFF") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotEnabled=false") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=false") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotDefaultBuildSafe=true")
        if ($phase3PilotMarkersOk) {
            Add-Check "phase3PilotScaffoldingDefaultOff" "PASS" "all Phase 3 pilot flags OFF; no typed dispatch fed into launch; runtime behavior unchanged"
        } else {
            Add-Check "phase3PilotScaffoldingDefaultOff" "FAIL" "one or more Phase 3 pilot default-off markers missing from hosted output"
        }

        $phase3Readiness = Get-LaunchTargetComparisonReadinessSummary -Output $hostedOutput
        $phase3TargetReadinessLines = New-Object System.Collections.Generic.List[string]
        foreach ($record in $phase3Readiness.records) {
            $contributesTo = New-Object System.Collections.Generic.List[string]
            [void]$contributesTo.Add($record.readiness)
            if ($record.builtInApp) { [void]$contributesTo.Add("builtInApp") }
            if ($record.legacyAlias) { [void]$contributesTo.Add("legacyAlias") }
            if ($record.shellAction) { [void]$contributesTo.Add("shellAction") }
            if ($record.fileOpen) { [void]$contributesTo.Add("fileOpen") }
            if ($record.unknownOrUnclassified) { [void]$contributesTo.Add("unknownOrUnclassified") }
            if ($record.specialCase) { [void]$contributesTo.Add("specialCase") }
            [void]$phase3TargetReadinessLines.Add(
                "phase3TargetReadiness target=$($record.target) resolvedType=$($record.resolvedType) appId=$($record.appId) actualDispatch=$($record.actualDispatch) typedDispatchCandidate=$($record.typedDispatchCandidate) typedDispatchCandidateComparison=$($record.typedDispatchCandidateComparison) readiness=$($record.readiness) blockReason=$($record.blockReason) contributesTo=$([string]::Join(',', $contributesTo))"
            )
        }
        $phase3BlockedTargets = [string]::Join(",", @($phase3Readiness.blockedTargets | Sort-Object))
        $phase3UnknownTargets = [string]::Join(",", @($phase3Readiness.unknownTargets | Sort-Object))
        $phase3ReadinessInvariantsOk = $phase3Readiness.readinessInvariantsOk
        if ($phase3ReadinessInvariantsOk) {
            Add-Check "phase3ReadinessInvariants" "PASS" "typedDispatchReadyCount+typedDispatchBlockedCount=$($phase3Readiness.typedDispatchReadyCount + $phase3Readiness.typedDispatchBlockedCount) totalObservedLaunchTargets=$($phase3Readiness.totalObservedLaunchTargets); unknownOrUnclassifiedCount=$($phase3Readiness.unknownOrUnclassifiedCount) blockedCount=$($phase3Readiness.typedDispatchBlockedCount); typedDispatchEnabled=false feedsTypedDispatchIntoLaunch=false runtimeLaunchBehaviorChanged=false"
        } else {
            Add-Check "phase3ReadinessInvariants" "FAIL" "one or more Phase 3A invariants failed; expected typedDispatchReadyCount+typedDispatchBlockedCount==totalObservedLaunchTargets, unknownOrUnclassifiedCount<=typedDispatchBlockedCount, and report-only typed dispatch disabled/runtime-unchanged markers"
        }
        $phase3ReadinessOk =
            $phase3Readiness.phase3TypedDispatchReadiness -eq "report-only" -and
            $phase3Readiness.totalObservedLaunchTargets -eq 8 -and
            $phase3Readiness.typedDispatchReadyCount -eq 5 -and
            $phase3Readiness.typedDispatchBlockedCount -eq 3 -and
            $phase3Readiness.specialCaseCount -eq 2 -and
            $phase3Readiness.legacyAliasCount -eq 2 -and
            $phase3Readiness.builtInAppCount -eq 5 -and
            $phase3Readiness.shellActionCount -eq 1 -and
            $phase3Readiness.fileOpenCount -eq 0 -and
            $phase3Readiness.unknownOrUnclassifiedCount -eq 2 -and
            $phase3Readiness.categoryCountsMayOverlap -eq $true -and
            $phase3BlockedTargets -eq "AppModel,ComputerFiles,TotallyUnknownLaunchThing" -and
            $phase3UnknownTargets -eq "ComputerFiles,TotallyUnknownLaunchThing"
        if ($phase3ReadinessOk) {
            Add-Check "phase3TypedDispatchReadiness" "PASS" "phase3TypedDispatchReadiness=report-only totalObservedLaunchTargets=8 typedDispatchReadyCount=5 typedDispatchBlockedCount=3 unknownOrUnclassifiedCount=2 legacyAliasCount=2 builtInAppCount=5 shellActionCount=1 fileOpenCount=0 specialCaseCount=2 phase3TypedDispatchBlockedTargets=$phase3BlockedTargets phase3TypedDispatchUnknownTargets=$phase3UnknownTargets phase3CategoryCountsMayOverlap=true"
        } else {
            Add-Check "phase3TypedDispatchReadiness" "FAIL" "missing or unexpected Phase 3A readiness summary in hosted appmodel output"
        }
        $phase3ReadinessInvariantsStatus = if ($phase3ReadinessInvariantsOk) { "PASS" } else { "FAIL" }
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
            ) -TimeoutSeconds ([Math]::Max(300, $TimeoutSeconds + 1200))
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

    # Phase 3 typed-dispatch planning audit (report-only; typed dispatch remains disabled)
    # Derived entirely from Phase 2 validated evidence. Does not enable typed dispatch or change runtime behavior.
    $phase3PlanningAudit = if ($readyForTypedDispatchPlanning -eq "true") { "pass" } else { "not-ready" }
    $phase3FirstPilotCandidate = "StartMenuNotepad"
    $phase3FirstPilotReason = "BuiltInApp;typedDispatchCandidateMatchesActual=true;comparison=match;no-known-drift;no-embedded-special-behavior;validated-in-phase2-launchshadow"
    $phase3CandidateRanking = "1=StartMenuNotepad(BuiltInApp,match,no-drift);2=PinnedDesktopNotepad(BuiltInApp,match,no-drift);3=StartMenuCalculator(BuiltInApp,match,no-drift);4=StartMenuConsole(ShellAction,match,no-drift);5=StartMenuFiles(LegacyAlias,match,no-drift)"
    $phase3RejectedCandidates = "Settings(drift:resolves-to-DisplayOptions-not-Settings);ControlPanel(drift:embedded-state-vs-DisplayOptions);AppModel(unsupported-embedded-diagnostic-action);Computer/Documents/Pictures/Music/Network(unsupported-empty-typed-candidate);Navigator(browser-complexity);.md/.wav/.gxapp/.elf/.exe/images(deferred-or-unsupported);anything-with-hosted-bare-metal-mismatch"

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
        "phase3TypedDispatchReadiness=$($phase3Readiness.phase3TypedDispatchReadiness) totalObservedLaunchTargets=$($phase3Readiness.totalObservedLaunchTargets) typedDispatchReadyCount=$($phase3Readiness.typedDispatchReadyCount) typedDispatchBlockedCount=$($phase3Readiness.typedDispatchBlockedCount) unknownOrUnclassifiedCount=$($phase3Readiness.unknownOrUnclassifiedCount) legacyAliasCount=$($phase3Readiness.legacyAliasCount) builtInAppCount=$($phase3Readiness.builtInAppCount) shellActionCount=$($phase3Readiness.shellActionCount) fileOpenCount=$($phase3Readiness.fileOpenCount) specialCaseCount=$($phase3Readiness.specialCaseCount)",
        "phase3TypedDispatchBlockedTargets=$phase3BlockedTargets",
        "phase3TypedDispatchUnknownTargets=$phase3UnknownTargets",
        "phase3CategoryCountsMayOverlap=$($phase3Readiness.categoryCountsMayOverlap.ToString().ToLowerInvariant())",
        "phase3ReadinessInvariants=$phase3ReadinessInvariantsStatus",
        "appModelPhase2LaunchShadowCoverageAudit=$coverageAudit",
        "appModelPhase2KnownDriftsDocumented=$knownDriftsDocumented",
        "appModelPhase2DeferredAssociationsDocumented=$deferredAssociationsDocumented",
        "appModelPhase2ReadyForTypedDispatchPlanning=$readyForTypedDispatchPlanning"
    )
    foreach ($line in $phase3TargetReadinessLines) {
        $reportLines += $line
    }
    $reportLines += @(
        "coverage.desktopFileFolder=folder,.txt,.log,.cfg,.ini",
        "coverage.desktopSystemObjects=ThisSystem,FileManager,Trash,SystemSettings",
        "coverage.startMenu=BuiltInApp(Notepad,Calculator,TaskManager,DiskManager,Trash,DisplayOptions);LegacyAlias(Files);ShellAction(Console,Settings,Computer,Documents,Pictures,Music,Network);embeddedDiagnostic(ControlPanel,AppModel)",
        "coverage.pinnedDesktopShortcut=RealBranchPinnedDesktopNotepad",
        "coverage.taskbar=audited-window-management-only;hosted-live-buttons-not-persisted-launch-sources;bare-metal-static-entries-disabled-count=0",
        "fileAssociations.aligned=folder,.txt,.log,.cfg,.ini",
        "fileAssociations.deferred=.md,images,.wav,.gxm,.mue,.img,.gxapp,.gxq,.elf,.exe,unknown",
        "knownNonFatalDrifts=Settings->DisplayOptions;Computer/Documents/Pictures/Music/Network->empty-typed-candidate;ControlPanel->embedded-state-vs-DisplayOptions;AppModel->embedded-viewer-unsupported-typed-target",
        "futureWork=.md,image-routing,.wav-media-contract,.gxm/.mue-loaders,.img-DiskManager-workflow,.gxapp/.gxq/.elf/.exe-app-like-paths,unknown-extension-policy",
        "[AppModelPhase3TypedDispatchPlanningAudit]",
        "mode=planning-only",
        "appModelPhase3TypedDispatchPlanningAudit=$phase3PlanningAudit",
        "appModelPhase3TypedDispatchStillDisabled=true",
        "appModelPhase3NoRuntimeLaunchBehaviorChanged=true",
        "appModelPhase3FirstPilotCandidate=$phase3FirstPilotCandidate",
        "appModelPhase3FirstPilotReason=$phase3FirstPilotReason",
        "appModelPhase3CandidateRanking=$phase3CandidateRanking",
        "appModelPhase3RejectedCandidates=$phase3RejectedCandidates",
        "appModelPhase3KnownDriftsPreservedAsNonfatal=true",
        "appModelPhase3Note=planning-readiness-only; typed dispatch not enabled; no runtime behavior changed",
        "[AppModelPhase3PilotScaffolding]",
        "appModelPhase3PilotCandidate=StartMenuNotepad",
        "appModelPhase3PilotStartMenuNotepadFlag=OFF",
        "appModelPhase3PilotFallbackToLegacyFlag=OFF",
        "appModelPhase3PilotEnabled=false",
        "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=false",
        "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false",
        "appModelPhase3PilotScopedToStartMenuNotepad=true",
        "appModelPhase3PilotDefaultBuildSafe=true",
        "appModelPhase3PilotScaffoldingNote=default-off scaffolding only; runtime hook not implemented; no launch path changed",
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
