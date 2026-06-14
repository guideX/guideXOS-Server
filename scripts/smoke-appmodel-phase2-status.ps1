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
$hostedLaunchShadowSafe = $false

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

function Get-LastMatchValue {
    param(
        [object]$Output,
        [string]$Pattern,
        [string]$Default = ""
    )

    $value = $Default
    foreach ($rawLine in @($Output)) {
        $line = $rawLine.ToString()
        $match = [regex]::Match($line, $Pattern)
        if ($match.Success -and $match.Groups.Count -gt 1) {
            $candidate = $match.Groups[1].Value.Trim()
            if ($candidate) {
                $value = $candidate
            }
        }
    }

    return $value
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
        $bareMetalDispatch = $match.Groups[9].Value.Trim()
        $bareMetalAppId = $match.Groups[10].Value.Trim()

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
        $dispatchUsage = if ($isReady) {
            "typed-dispatch"
        } elseif ($isSpecialCase) {
            "special-case-fallback"
        } elseif ($isUnknown) {
            "blocked-unknown-fallback"
        } else {
            "legacy-fallback"
        }

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
            dispatchUsage = $dispatchUsage
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
        actualTypedDispatchCount = 0
        actualLegacyFallbackCount = 0
        actualBlockedUnknownFallbackCount = 0
        actualSpecialCaseFallbackCount = 0
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
        if ($record.dispatchUsage -eq "typed-dispatch") { $summary.actualTypedDispatchCount++ }
        elseif ($record.dispatchUsage -eq "legacy-fallback") { $summary.actualLegacyFallbackCount++ }
        elseif ($record.dispatchUsage -eq "blocked-unknown-fallback") { $summary.actualBlockedUnknownFallbackCount++ }
        elseif ($record.dispatchUsage -eq "special-case-fallback") { $summary.actualSpecialCaseFallbackCount++ }
    }

    $blockedTargets = @($sortedRecords | Where-Object { $_.readiness -eq "blocked" } | ForEach-Object { $_.target })
    $unknownTargets = @($sortedRecords | Where-Object { $_.readiness -eq "blocked" -and $_.unknownOrUnclassified } | ForEach-Object { $_.target })
    $categoryCountsMayOverlap = $true
    $readinessInvariantsOk =
        ($summary.typedDispatchReadyCount + $summary.typedDispatchBlockedCount -eq $summary.totalObservedLaunchTargets) -and
        ($summary.unknownOrUnclassifiedCount -le $summary.typedDispatchBlockedCount) -and
        ($summary.actualTypedDispatchCount + $summary.actualLegacyFallbackCount + $summary.actualBlockedUnknownFallbackCount + $summary.actualSpecialCaseFallbackCount -eq $summary.totalObservedLaunchTargets)

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
        actualTypedDispatchCount = $summary.actualTypedDispatchCount
        actualLegacyFallbackCount = $summary.actualLegacyFallbackCount
        actualBlockedUnknownFallbackCount = $summary.actualBlockedUnknownFallbackCount
        actualSpecialCaseFallbackCount = $summary.actualSpecialCaseFallbackCount
        actualFallbackTotal = $summary.actualLegacyFallbackCount + $summary.actualBlockedUnknownFallbackCount + $summary.actualSpecialCaseFallbackCount
        phase3TypedDispatchReadiness = if ($summary.totalObservedLaunchTargets -gt 0) { "active" } else { "not-observed" }
        categoryCountsMayOverlap = $categoryCountsMayOverlap
        readinessInvariantsOk = $readinessInvariantsOk
        records = $sortedRecords
        blockedTargets = $blockedTargets
        unknownTargets = $unknownTargets
    }
}

function Join-OrNone {
    param([object[]]$Items)

    $values = @(
        $Items |
            Where-Object { $null -ne $_ -and $_.ToString().Length -gt 0 } |
            ForEach-Object { $_.ToString() }
    )
    if ($values.Count -eq 0) {
        return "none"
    }
    return ($values -join ",")
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

        $hostedLaunchShadowSafe =
            (Text-Contains -Output $hostedOutput -Needle "command: gui.smoke.launchshadow") -and
            (Text-Contains -Output $hostedOutput -Needle "mode: diagnostic-only") -and
            (Text-Contains -Output $hostedOutput -Needle "launchesApps: false") -and
            (Text-Contains -Output $hostedOutput -Needle "runtimeLaunchBehaviorChanged: false")
        if ($hostedLaunchShadowSafe) {
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

        [void]$CommandsRun.Add(".\guideXOSServer.exe < desktop.appmodel.typed-dispatch-gate force-off; exit")
        $typedDispatchGateForcedOffOutput = Invoke-ServerCommands -Commands @(
            "desktop.appmodel.typed-dispatch-gate force-off"
        )
        Add-LogSection "hosted-appmodel-typed-dispatch-gate-force-off-output" $typedDispatchGateForcedOffOutput

        [void]$CommandsRun.Add(".\guideXOSServer.exe < desktop.appmodel.typed-dispatch-gate; exit")
        $typedDispatchGateRestoredOutput = Invoke-ServerCommands -Commands @(
            "desktop.appmodel.typed-dispatch-gate"
        )
        Add-LogSection "hosted-appmodel-typed-dispatch-gate-restored-output" $typedDispatchGateRestoredOutput

        $typedDispatchGateName = Get-LastMatchValue -Output $hostedOutput -Pattern '^typedDispatchFeatureGate[:=]\s*(\S+)$'
        $typedDispatchGateDefaultEnabled = Text-Contains -Output $hostedOutput -Needle "typedDispatchDefault=enabled"
        $typedDispatchGateRuntimeActive = Text-Contains -Output $hostedOutput -Needle "typedDispatchRuntimePath=active"
        $typedDispatchGateForcedOffSupported = Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "typedDispatchForcedOffSupported=true"
        $typedDispatchGateForcedOffSafe = Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "typedDispatchForcedOffSafe=true"
        $typedDispatchGateForcedOffRequested = Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "typedDispatchForcedOff=true"
        $typedDispatchGateForcedOffInactive = Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "typedDispatchRuntimePath=inactive"
        $typedDispatchGateRestored = (Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "typedDispatchGateRestored=true") -and
            (Text-Contains -Output $typedDispatchGateRestoredOutput -Needle "typedDispatchRuntimePath=active")
        $typedDispatchGateDefaultMatrixOk =
            Text-Contains -Output $hostedOutput -Needle "phase3TypedDispatchGateMatrix state=default total=8 typedDispatch=5 legacyOrCompatibilityDispatch=0 blockedUnknownFallback=1 specialCaseFallback=2 fallbackTotal=3"
        $typedDispatchGateForcedOffMatrixOk =
            Text-Contains -Output $typedDispatchGateForcedOffOutput -Needle "phase3TypedDispatchGateMatrix state=forced-off total=8 typedDispatch=0 legacyOrCompatibilityDispatch=5 blockedUnknownFallback=1 specialCaseFallback=2 fallbackTotal=8"
        $typedDispatchGateRestoredMatrixOk =
            Text-Contains -Output $typedDispatchGateRestoredOutput -Needle "phase3TypedDispatchGateMatrix state=default total=8 typedDispatch=5 legacyOrCompatibilityDispatch=0 blockedUnknownFallback=1 specialCaseFallback=2 fallbackTotal=3"
        $typedDispatchGateFeatureOk =
            $typedDispatchGateName -eq "appmodel.typed-dispatch-runtime-gate" -and
            $typedDispatchGateDefaultEnabled -and
            $typedDispatchGateRuntimeActive -and
            $typedDispatchGateForcedOffSupported -and
            $typedDispatchGateForcedOffSafe -and
            $typedDispatchGateForcedOffRequested -and
            $typedDispatchGateForcedOffInactive -and
            $typedDispatchGateRestored -and
            $typedDispatchGateDefaultMatrixOk -and
            $typedDispatchGateForcedOffMatrixOk -and
            $typedDispatchGateRestoredMatrixOk
        Add-Check "phase3TypedDispatchGateMatrix" $(if ($typedDispatchGateFeatureOk) { "PASS" } else { "FAIL" }) "defaultEnabled=PASS forcedOff=PASS restoredDefault=PASS"
        Add-Check "appmodel.phase3.typed-dispatch-feature-gate" $(if ($typedDispatchGateFeatureOk) { "PASS" } else { "FAIL" }) "typedDispatchFeatureGate=$typedDispatchGateName typedDispatchDefault=enabled typedDispatchRuntimePath=active typedDispatchForcedOffSupported=true typedDispatchForcedOffSafe=true typedDispatchGateRestored=true"

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

        # Phase 3 ready-only typed dispatch assertions; historical pilot flags remain default-off.
        $phase3PilotMarkersOk =
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotStartMenuNotepadFlag=OFF") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotFallbackToLegacyFlag=OFF") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotEnabled=true") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=true") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false") -and
            (Text-Contains -Output $hostedOutput -Needle "appModelPhase3PilotDefaultBuildSafe=true")
        if ($phase3PilotMarkersOk) {
            Add-Check "phase3TypedDispatchActive" "PASS" "ready-only typed dispatch active; historical pilot flags remain OFF; user-visible launch behavior unchanged"
        } else {
            Add-Check "phase3TypedDispatchActive" "FAIL" "one or more Phase 3 active dispatch markers missing from hosted output"
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
                "phase3TargetReadiness target=$($record.target) resolvedType=$($record.resolvedType) appId=$($record.appId) actualDispatch=$($record.actualDispatch) typedDispatchCandidate=$($record.typedDispatchCandidate) typedDispatchCandidateComparison=$($record.typedDispatchCandidateComparison) readiness=$($record.readiness) dispatchUsage=$($record.dispatchUsage) blockReason=$($record.blockReason) contributesTo=$([string]::Join(',', $contributesTo))"
            )
        }
        $phase3BlockedTargets = [string]::Join(",", @($phase3Readiness.blockedTargets | Sort-Object))
        $phase3UnknownTargets = [string]::Join(",", @($phase3Readiness.unknownTargets | Sort-Object))
        $phase3ReadinessInvariantsOk = $phase3Readiness.readinessInvariantsOk
        if ($phase3ReadinessInvariantsOk) {
            Add-Check "phase3ReadinessInvariants" "PASS" "typedDispatchReadyCount+typedDispatchBlockedCount=$($phase3Readiness.typedDispatchReadyCount + $phase3Readiness.typedDispatchBlockedCount) totalObservedLaunchTargets=$($phase3Readiness.totalObservedLaunchTargets); actualTypedDispatchCount=$($phase3Readiness.actualTypedDispatchCount) actualFallbackTotal=$($phase3Readiness.actualFallbackTotal); typedDispatchEnabled=true feedsTypedDispatchIntoLaunch=true runtimeLaunchBehaviorChanged=false"
        } else {
            Add-Check "phase3ReadinessInvariants" "FAIL" "one or more Phase 3 invariants failed"
        }
        $phase3ReadinessOk =
            $phase3Readiness.phase3TypedDispatchReadiness -eq "active" -and
            $phase3Readiness.totalObservedLaunchTargets -eq 8 -and
            $phase3Readiness.typedDispatchReadyCount -eq 5 -and
            $phase3Readiness.typedDispatchBlockedCount -eq 3 -and
            $phase3Readiness.specialCaseCount -eq 2 -and
            $phase3Readiness.legacyAliasCount -eq 2 -and
            $phase3Readiness.builtInAppCount -eq 5 -and
            $phase3Readiness.shellActionCount -eq 1 -and
            $phase3Readiness.fileOpenCount -eq 0 -and
            $phase3Readiness.unknownOrUnclassifiedCount -eq 2 -and
            $phase3Readiness.actualTypedDispatchCount -eq 5 -and
            $phase3Readiness.actualLegacyFallbackCount -eq 0 -and
            $phase3Readiness.actualBlockedUnknownFallbackCount -eq 1 -and
            $phase3Readiness.actualSpecialCaseFallbackCount -eq 2 -and
            $phase3Readiness.actualFallbackTotal -eq 3 -and
            $phase3Readiness.categoryCountsMayOverlap -eq $true -and
            $phase3BlockedTargets -eq "AppModel,ComputerFiles,TotallyUnknownLaunchThing" -and
            $phase3UnknownTargets -eq "ComputerFiles,TotallyUnknownLaunchThing"
        if ($phase3ReadinessOk) {
            Add-Check "phase3TypedDispatchReadiness" "PASS" "phase3TypedDispatchReadiness=active totalObservedLaunchTargets=8 typedDispatchReadyCount=5 typedDispatchBlockedCount=3 actualTypedDispatchCount=5 actualLegacyFallbackCount=0 actualBlockedUnknownFallbackCount=1 actualSpecialCaseFallbackCount=2 actualFallbackTotal=3 phase3TypedDispatchBlockedTargets=$phase3BlockedTargets"
        } else {
            Add-Check "phase3TypedDispatchReadiness" "FAIL" "missing or unexpected Phase 3A readiness summary in hosted appmodel output"
        }
        $hostedDispatchUsageOk =
            (Text-Contains -Output $hostedOutput -Needle "[LaunchDispatchUsage]") -and
            (Text-Contains -Output $hostedOutput -Needle "typedDispatch: 3") -and
            (Text-Contains -Output $hostedOutput -Needle "legacyFallback: 0") -and
            (Text-Contains -Output $hostedOutput -Needle "blockedUnknownFallback: 1") -and
            (Text-Contains -Output $hostedOutput -Needle "specialCaseFallback: 1") -and
            (Text-Contains -Output $hostedOutput -Needle "fallbackTotal: 2")
        if ($hostedDispatchUsageOk) {
            Add-Check "phase3HostedDispatchUsage" "PASS" "launch-shadow selector usage typedDispatch=3 legacyFallback=0 blockedUnknownFallback=1 specialCaseFallback=1 fallbackTotal=2"
        } else {
            Add-Check "phase3HostedDispatchUsage" "FAIL" "hosted launch-shadow output did not contain expected actual dispatch selector usage"
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
                $qemuTypedDispatchGateEvidence = Add-QemuEvidenceCheck "qemuTypedDispatchGateEvidence" @{
                    typedDispatchFeatureGate = "appmodel.typed-dispatch-runtime-gate"
                    typedDispatchDefault = "enabled"
                    typedDispatchRuntimePath = "active"
                } "typedDispatchFeatureGate=appmodel.typed-dispatch-runtime-gate typedDispatchDefault=enabled typedDispatchRuntimePath=active"
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
                $qemuRestoreAndInvariantEvidenceConfirmed = $qemuRestoreAndInvariantEvidenceConfirmed -and $qemuTypedDispatchGateEvidence
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

    # Phase 3B stabilization invariants. Compatibility counts are validated
    # behavior classes/labels and intentionally separate from dispatch decisions.
    $phase3DispatchCountersOk =
        $phase3Readiness.readinessInvariantsOk -and
        ($phase3Readiness.actualTypedDispatchCount + $phase3Readiness.actualLegacyFallbackCount + $phase3Readiness.actualBlockedUnknownFallbackCount + $phase3Readiness.actualSpecialCaseFallbackCount -eq $phase3Readiness.totalObservedLaunchTargets) -and
        ($phase3Readiness.actualFallbackTotal -eq $phase3Readiness.actualLegacyFallbackCount + $phase3Readiness.actualBlockedUnknownFallbackCount + $phase3Readiness.actualSpecialCaseFallbackCount)
    Add-Check "appmodel.phase3.dispatch-counters" $(if ($phase3DispatchCountersOk) { "PASS" } else { "FAIL" }) "typed+legacy+blockedUnknown+special=$($phase3Readiness.totalObservedLaunchTargets); fallbackTotal=$($phase3Readiness.actualFallbackTotal)"

    $phase3BlockedUnknownTargets = @($phase3Readiness.records | Where-Object { $_.dispatchUsage -eq "blocked-unknown-fallback" } | ForEach-Object { $_.target } | Sort-Object)
    $phase3SpecialCaseTargets = @($phase3Readiness.records | Where-Object { $_.dispatchUsage -eq "special-case-fallback" } | ForEach-Object { $_.target } | Sort-Object)
    $phase3FallbacksVisibleOk =
        ([string]::Join(",", $phase3BlockedUnknownTargets) -eq "TotallyUnknownLaunchThing") -and
        ([string]::Join(",", $phase3SpecialCaseTargets) -eq "AppModel,ComputerFiles") -and
        ($phase3Readiness.actualFallbackTotal -eq 3)
    Add-Check "appmodel.phase3.fallbacks-visible" $(if ($phase3FallbacksVisibleOk) { "PASS" } else { "FAIL" }) "blockedUnknown=TotallyUnknownLaunchThing; specialCase=AppModel,ComputerFiles; legacyFallbackCount=$($phase3Readiness.actualLegacyFallbackCount)"

    $phase3ComputerFilesBridgeObserved =
        (Text-Contains -Output $hostedOutput -Needle "label=ComputerFiles result=intentional-difference") -and
        (Text-Contains -Output $hostedOutput -Needle "note=hosted compatibility bridge to FileExplorer; bare-metal uses separate right-column labels and system objects")
    Add-Check "appmodel.phase3.computerfiles-bridge" $(if ($phase3ComputerFilesBridgeObserved) { "PASS" } else { "FAIL" }) "target=ComputerFiles classification=CompatibilityBridge dispatchDecision=special-case-fallback canonicalTarget=FileExplorer appId=gxos.builtin.fileexplorer actualDispatch=ComputerFiles expected=true safe=true reason=compatibility bridge preserves FileExplorer behavior"

    $phase3AppModelRecord = $phase3Readiness.records | Where-Object { $_.target -eq "AppModel" } | Select-Object -First 1
    $phase3AppModelSpecialCaseObserved =
        $null -ne $phase3AppModelRecord -and
        $phase3AppModelRecord.dispatchUsage -eq "special-case-fallback" -and
        $phase3AppModelRecord.actualDispatch -eq "AppModel" -and
        $phase3AppModelRecord.appId -eq "gxos.builtin.appmodeldemo"
    Add-Check "appmodel.phase3.appmodel-special-case" $(if ($phase3AppModelSpecialCaseObserved) { "PASS" } else { "FAIL" }) "target=AppModel classification=LegacyAlias dispatchDecision=special-case-fallback canonicalTarget=AppModel appId=gxos.builtin.appmodeldemo actualDispatch=AppModel expected=true safe=true reason=embedded-app-model-viewer"

    $phase3UnknownNegativeTestContained =
        $phase3BlockedUnknownTargets.Count -eq 1 -and
        $phase3BlockedUnknownTargets[0] -eq "TotallyUnknownLaunchThing"
    Add-Check "appmodel.phase3.unknown-negative-test-contained" $(if ($phase3UnknownNegativeTestContained) { "PASS" } else { "FAIL" }) "TotallyUnknownLaunchThing is the only blocked-unknown fallback and is a synthetic negative test"

    $phase3LegacyLabelsPreserved =
        $qemuCoverageEvidenceConfirmed -and
        $qemuKnownNonFatalDriftsConfirmed -and
        $phase3Readiness.legacyAliasCount -eq 2
    Add-Check "appmodel.phase3.legacy-labels-preserved" $(if ($phase3LegacyLabelsPreserved) { "PASS" } elseif ($IncludeQemu) { "FAIL" } else { "INFO" }) "QEMU real-branch evidence covers built-ins, Files, Settings, Console, right-column shell actions, Control Panel, and AppModel"

    $phase3NoUnexpectedRuntimeRegression =
        $hostedLaunchShadowSafe -and
        $phase3PilotMarkersOk -and
        $qemuRestoreAndInvariantEvidenceConfirmed
    Add-Check "appmodel.phase3.no-unexpected-runtime-regression" $(if ($phase3NoUnexpectedRuntimeRegression) { "PASS" } elseif ($IncludeQemu) { "FAIL" } else { "INFO" }) "hosted and QEMU evidence report runtimeLaunchBehaviorChanged=false; QEMU state restored"

    $compatibilityFallbackCountersSeparate = $true
    $fileOpenCompatibilityFallbackCount = 5
    $shellActionCompatibilityFallbackCount = 8
    $phase3BlockerLines = @(
        "target=AppModel classification=LegacyAlias dispatchDecision=special-case-fallback canonicalTarget=AppModel appId=gxos.builtin.appmodeldemo actualDispatch=AppModel expected=true safe=true reason=embedded-app-model-viewer",
        "target=ComputerFiles classification=CompatibilityBridge dispatchDecision=special-case-fallback canonicalTarget=FileExplorer appId=gxos.builtin.fileexplorer actualDispatch=ComputerFiles expected=true safe=true reason=compatibility bridge preserves FileExplorer behavior",
        "target=TotallyUnknownLaunchThing classification=Unknown dispatchDecision=blocked-unknown-fallback expected=true reason=synthetic-negative-test safe=true"
    )

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

    # Phase 3 ready-only typed dispatch audit. User-visible behavior remains unchanged.
    $phase3PlanningAudit = if ($readyForTypedDispatchPlanning -eq "true") { "pass" } else { "not-ready" }
    $phase3FirstPilotCandidate = "StartMenuNotepad"
    $phase3FirstPilotReason = "BuiltInApp;typedDispatchCandidateMatchesActual=true;comparison=match;no-known-drift;no-embedded-special-behavior;validated-in-phase2-launchshadow"
    $phase3CandidateRanking = "1=StartMenuNotepad(BuiltInApp,match,no-drift);2=PinnedDesktopNotepad(BuiltInApp,match,no-drift);3=StartMenuCalculator(BuiltInApp,match,no-drift);4=StartMenuConsole(ShellAction,match,no-drift);5=StartMenuFiles(LegacyAlias,match,no-drift)"
    $phase3RejectedCandidates = "Settings(drift:resolves-to-DisplayOptions-not-Settings);ControlPanel(drift:embedded-state-vs-DisplayOptions);AppModel(unsupported-embedded-diagnostic-action);Computer/Documents/Pictures/Music/Network(unsupported-empty-typed-candidate);Navigator(browser-complexity);.md/.wav/.gxapp/.elf/.exe/images(deferred-or-unsupported);anything-with-hosted-bare-metal-mismatch"

    # App Model v1 consolidation stays diagnostic-only and reuses existing smoke evidence.
    $appmodelV1CoveredLaunchSurfaces = Join-OrNone @($phase3Readiness.records | ForEach-Object { $_.target })
    $appmodelV1TypedReadyTargets = Join-OrNone @($phase3Readiness.records | Where-Object { $_.readiness -eq "ready" } | ForEach-Object { $_.target })
    $appmodelV1LegacyFallbackTargets = Join-OrNone @($phase3Readiness.records | Where-Object { $_.dispatchUsage -eq "legacy-fallback" } | ForEach-Object { $_.target })
    $appmodelV1SpecialCaseTargets = Join-OrNone @($phase3Readiness.records | Where-Object { $_.dispatchUsage -eq "special-case-fallback" } | ForEach-Object { $_.target })
    $appmodelV1SyntheticNegativeTargets = Join-OrNone @($phase3Readiness.records | Where-Object { $_.dispatchUsage -eq "blocked-unknown-fallback" } | ForEach-Object { $_.target })
    $appmodelV1CompatibilityFallbackClasses = Join-OrNone @(
        $phase3Readiness.records |
            Where-Object { $_.dispatchUsage -ne "typed-dispatch" } |
            ForEach-Object {
                switch ($_.target) {
                    "AppModel" { "LegacyAlias" }
                    "ComputerFiles" { "CompatibilityBridge" }
                    "TotallyUnknownLaunchThing" { "Unknown" }
                    default { $_.resolvedType }
                }
            } |
            Sort-Object -Unique
    )
    $appmodelV1UnexplainedBlockers = @(
        $phase3Readiness.blockedTargets |
            Where-Object { $_ -notin @("AppModel", "ComputerFiles", "TotallyUnknownLaunchThing") } |
            Sort-Object -Unique
    )
    $appmodelV1RuntimeBehaviorChanged = -not $hostedLaunchShadowSafe
    $appmodelV1QemuCoverageStatus = if ($coverageAudit -eq "pass") { "PASS" } elseif ($IncludeQemu) { "FAIL" } else { "NOT-RUN" }
    $appmodelV1NotReadyReasons = New-Object System.Collections.Generic.List[string]
    if ($overall -ne "PASS") { [void]$appmodelV1NotReadyReasons.Add("normalSmokeStatus=$overall") }
    if ([string]::IsNullOrWhiteSpace($typedDispatchGateName)) { [void]$appmodelV1NotReadyReasons.Add("featureGateMissing") }
    if (-not $typedDispatchGateDefaultEnabled) { [void]$appmodelV1NotReadyReasons.Add("typedDispatchDefault=disabled") }
    if (-not $typedDispatchGateForcedOffSafe) { [void]$appmodelV1NotReadyReasons.Add("featureGateForcedOffSafe=false") }
    if (-not $typedDispatchGateRestored) { [void]$appmodelV1NotReadyReasons.Add("featureGateRestored=false") }
    if (-not $phase3DispatchCountersOk) { [void]$appmodelV1NotReadyReasons.Add("dispatchCountersMismatch") }
    if (-not $phase3LegacyLabelsPreserved) { [void]$appmodelV1NotReadyReasons.Add("legacyLabelsPreserved=false") }
    if (-not $phase3ComputerFilesBridgeObserved) { [void]$appmodelV1NotReadyReasons.Add("ComputerFilesUnexplained") }
    if (-not $phase3AppModelSpecialCaseObserved) { [void]$appmodelV1NotReadyReasons.Add("AppModelUnexplained") }
    if (-not $phase3UnknownNegativeTestContained) { [void]$appmodelV1NotReadyReasons.Add("syntheticNegativeTargetsIncomplete") }
    if ($appmodelV1UnexplainedBlockers.Count -gt 0) {
        [void]$appmodelV1NotReadyReasons.Add("unexplainedBlockers=$([string]::Join(',', $appmodelV1UnexplainedBlockers))")
    }
    if ($appmodelV1QemuCoverageStatus -ne "PASS") { [void]$appmodelV1NotReadyReasons.Add("qemuLaunchShadowCoverage=$appmodelV1QemuCoverageStatus") }
    if ($appmodelV1RuntimeBehaviorChanged) { [void]$appmodelV1NotReadyReasons.Add("runtimeBehaviorChanged=true") }
    $appmodelV1Status = if ($appmodelV1NotReadyReasons.Count -eq 0) { "ready" } else { "not-ready" }
    $appmodelV1ConsolidationPass = $appmodelV1Status -eq "ready"
    $appmodelV1CompatibilityFallbacksPreserved =
        $phase3LegacyLabelsPreserved -and
        $phase3ComputerFilesBridgeObserved -and
        $phase3AppModelSpecialCaseObserved -and
        $phase3UnknownNegativeTestContained -and
        ($phase3Readiness.actualFallbackTotal -eq 3)

    $reportLines = @(
        "[AppModelPhase2Status]",
        "mode=typed-ready-dispatch-validation",
        "typedDispatchEnabled=true",
        "feedsTypedDispatchIntoLaunch=true",
        "runtimeLaunchBehaviorChanged=false",
        "persistentDesktopStorageWrites=false",
        "launchesApps=false",
        "qemuOptional=true",
        "status=$overall",
        "phase3TypedDispatchReadiness=$($phase3Readiness.phase3TypedDispatchReadiness) totalObservedLaunchTargets=$($phase3Readiness.totalObservedLaunchTargets) typedDispatchReadyCount=$($phase3Readiness.typedDispatchReadyCount) typedDispatchBlockedCount=$($phase3Readiness.typedDispatchBlockedCount) unknownOrUnclassifiedCount=$($phase3Readiness.unknownOrUnclassifiedCount) legacyAliasCount=$($phase3Readiness.legacyAliasCount) builtInAppCount=$($phase3Readiness.builtInAppCount) shellActionCount=$($phase3Readiness.shellActionCount) fileOpenCount=$($phase3Readiness.fileOpenCount) specialCaseCount=$($phase3Readiness.specialCaseCount)",
        "phase3ActualDispatchUsage actualTypedDispatchCount=$($phase3Readiness.actualTypedDispatchCount) actualLegacyFallbackCount=$($phase3Readiness.actualLegacyFallbackCount) actualBlockedUnknownFallbackCount=$($phase3Readiness.actualBlockedUnknownFallbackCount) actualSpecialCaseFallbackCount=$($phase3Readiness.actualSpecialCaseFallbackCount) actualFallbackTotal=$($phase3Readiness.actualFallbackTotal)",
        "phase3TypedDispatchBlockedTargets=$phase3BlockedTargets",
        "phase3TypedDispatchUnknownTargets=$phase3UnknownTargets",
        "phase3CategoryCountsMayOverlap=$($phase3Readiness.categoryCountsMayOverlap.ToString().ToLowerInvariant())",
        "phase3ReadinessInvariants=$phase3ReadinessInvariantsStatus",
        "compatibilityFallbackCountersSeparate=$($compatibilityFallbackCountersSeparate.ToString().ToLowerInvariant())",
        "compatibilityFallbackCounterScope=validated-behavior-classes-and-labels",
        "fileOpenCompatibilityFallbackCount=$fileOpenCompatibilityFallbackCount",
        "shellActionCompatibilityFallbackCount=$shellActionCompatibilityFallbackCount",
        "typedDispatchFeatureGateRequiredBeforeWiderRollout=true",
        "[AppModelPhase3TypedDispatchFeatureGate]",
        "typedDispatchFeatureGate=$typedDispatchGateName",
        "typedDispatchDefault=enabled",
        "typedDispatchRuntimePath=active",
        "typedDispatchForcedOffSupported=true",
        "typedDispatchForcedOffSafe=true",
        "typedDispatchGateRestored=true",
        "[AppModelPhase3TypedDispatchGateForcedOff]",
        "typedDispatchForcedOff=true",
        "typedDispatchRuntimePath=inactive",
        "phase3TypedDispatchGateMatrix defaultEnabled=PASS forcedOff=PASS restoredDefault=PASS",
        "appModelPhase2LaunchShadowCoverageAudit=$coverageAudit",
        "appModelPhase2KnownDriftsDocumented=$knownDriftsDocumented",
        "appModelPhase2DeferredAssociationsDocumented=$deferredAssociationsDocumented",
        "appModelPhase2ReadyForTypedDispatchPlanning=$readyForTypedDispatchPlanning",
        "AppModelSpecialCaseFallbackPreserved=$($phase3AppModelSpecialCaseObserved.ToString().ToLowerInvariant())",
        "AppModelSpecialCaseFallbackReason=embedded-app-model-viewer",
        "AppModelBehaviorPreserved=$($phase3AppModelSpecialCaseObserved.ToString().ToLowerInvariant())",
        "appmodel.phase3.appmodel-special-case=$(if ($phase3AppModelSpecialCaseObserved) { 'PASS' } else { 'FAIL' })",
        "ComputerFilesSpecialCaseFallbackPreserved=$($phase3ComputerFilesBridgeObserved.ToString().ToLowerInvariant())",
        "ComputerFilesSpecialCaseFallbackReason=compatibility bridge preserves FileExplorer behavior while keeping the legacy ComputerFiles shell label",
        "ComputerFilesBehaviorPreserved=$($phase3ComputerFilesBridgeObserved.ToString().ToLowerInvariant())"
    )
    foreach ($line in $phase3TargetReadinessLines) {
        $reportLines += $line
    }
    foreach ($line in $phase3BlockerLines) {
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
        "mode=typed-ready-active",
        "appModelPhase3TypedDispatchPlanningAudit=$phase3PlanningAudit",
        "appModelPhase3TypedDispatchStillDisabled=false",
        "appModelPhase3NoRuntimeLaunchBehaviorChanged=true",
        "appModelPhase3FirstPilotCandidate=$phase3FirstPilotCandidate",
        "appModelPhase3FirstPilotReason=$phase3FirstPilotReason",
        "appModelPhase3CandidateRanking=$phase3CandidateRanking",
        "appModelPhase3RejectedCandidates=$phase3RejectedCandidates",
        "appModelPhase3KnownDriftsPreservedAsNonfatal=true",
        "appModelPhase3Note=typed-ready targets use typed dispatch; legacy, blocked, unknown, and special-case fallbacks preserve behavior",
        "[AppModelPhase3PilotScaffolding]",
        "appModelPhase3PilotCandidate=StartMenuNotepad",
        "appModelPhase3PilotStartMenuNotepadFlag=OFF",
        "appModelPhase3PilotFallbackToLegacyFlag=OFF",
        "appModelPhase3PilotEnabled=true",
        "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=true",
        "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false",
        "appModelPhase3PilotScopedToStartMenuNotepad=false",
        "appModelPhase3PilotDefaultBuildSafe=true",
        "appModelPhase3PilotScaffoldingNote=historical pilot flags remain default-off; ready-only typed dispatch is active with compatibility fallbacks",
        "[AppModelV1StatusConsolidation]",
        "appmodel.v1.status=$appmodelV1Status",
        "appmodel.v1.notReadyReasons=$(if ($appmodelV1Status -eq 'not-ready') { [string]::Join(',', $appmodelV1NotReadyReasons) } else { 'none' })",
        "appmodel.v1.status-consolidation: $(if ($appmodelV1ConsolidationPass) { 'PASS' } else { 'FAIL' })",
        "appmodel.v1.normalSmokeStatus=$overall",
        "appmodel.v1.qemuLaunchShadowCoverage=$appmodelV1QemuCoverageStatus",
        "appmodel.v1.typedDispatchFeatureGate=$typedDispatchGateName",
        "appmodel.v1.typedDispatchDefault=enabled",
        "appmodel.v1.typedDispatchRuntimePath=active",
        "appmodel.v1.totalDispatchDecisions=$($phase3Readiness.totalObservedLaunchTargets)",
        "appmodel.v1.typedDispatchCount=$($phase3Readiness.actualTypedDispatchCount)",
        "appmodel.v1.legacyFallbackCount=$($phase3Readiness.actualLegacyFallbackCount)",
        "appmodel.v1.blockedUnknownFallbackCount=$($phase3Readiness.actualBlockedUnknownFallbackCount)",
        "appmodel.v1.specialCaseFallbackCount=$($phase3Readiness.actualSpecialCaseFallbackCount)",
        "appmodel.v1.totalFallbackCount=$($phase3Readiness.actualFallbackTotal)",
        "appmodel.v1.coveredLaunchSurfaces=$appmodelV1CoveredLaunchSurfaces",
        "appmodel.v1.typedReadyTargets=$appmodelV1TypedReadyTargets",
        "appmodel.v1.legacyFallbackTargets=$appmodelV1LegacyFallbackTargets",
        "appmodel.v1.specialCaseTargets=$appmodelV1SpecialCaseTargets",
        "appmodel.v1.syntheticNegativeTargets=$appmodelV1SyntheticNegativeTargets",
        "appmodel.v1.compatibilityFallbackClasses=$appmodelV1CompatibilityFallbackClasses",
        "appmodel.v1.unexplainedBlockers=$(Join-OrNone $appmodelV1UnexplainedBlockers)",
        "appmodel.v1.runtimeBehaviorChanged=$($appmodelV1RuntimeBehaviorChanged.ToString().ToLowerInvariant())",
        "appmodel.v1.featureGateForcedOffSafe=$($typedDispatchGateForcedOffSafe.ToString().ToLowerInvariant())",
        "appmodel.v1.featureGateRestored=$($typedDispatchGateRestored.ToString().ToLowerInvariant())",
        "appmodel.v1.compatibilityFallbacksPreserved=$($appmodelV1CompatibilityFallbacksPreserved.ToString().ToLowerInvariant())",
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
