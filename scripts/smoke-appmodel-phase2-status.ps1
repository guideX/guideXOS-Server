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

$Checks = New-Object System.Collections.Generic.List[object]
$CommandsRun = New-Object System.Collections.Generic.List[string]
$CommandsListed = New-Object System.Collections.Generic.List[string]
$LogSections = New-Object System.Collections.Generic.List[string]

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
    return (($Output -join [Environment]::NewLine).Contains($Needle))
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

function Invoke-ProcessCommand {
    param(
        [string]$Name,
        [string]$FilePath,
        [string[]]$ArgumentList
    )

    $stdoutPath = Join-Path $LogDir "appmodel-phase2-status-$stamp-$Name.out.log"
    $stderrPath = Join-Path $LogDir "appmodel-phase2-status-$stamp-$Name.err.log"
    $proc = Start-Process -FilePath $FilePath `
        -ArgumentList $ArgumentList `
        -PassThru `
        -Wait `
        -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath

    $output = New-Object System.Collections.Generic.List[string]
    if (Test-Path $stdoutPath) {
        foreach ($line in (Get-Content $stdoutPath)) { [void]$output.Add($line) }
    }
    if (Test-Path $stderrPath) {
        foreach ($line in (Get-Content $stderrPath)) { [void]$output.Add($line) }
    }

    return [pscustomobject]@{
        Output = $output
        ExitCode = $proc.ExitCode
    }
}

Push-Location $Root
try {
    $hostedBuildCommand = ".\build.bat"
    if ($BuildHosted) {
        [void]$CommandsRun.Add($hostedBuildCommand)
        $buildResult = Invoke-ProcessCommand -Name "hosted-build" -FilePath "cmd.exe" -ArgumentList @("/c", "`"$(Join-Path $Root "build.bat")`"")
        Add-LogSection "hosted-build-output" $buildResult.Output
        if ($buildResult.ExitCode -eq 0) {
            Add-Check "hostedBuild" "PASS" "command=$hostedBuildCommand"
        } else {
            Add-Check "hostedBuild" "FAIL" "command=$hostedBuildCommand exitCode=$($buildResult.ExitCode)"
        }
    } else {
        [void]$CommandsListed.Add("$hostedBuildCommand (pass -BuildHosted to run)")
        Add-Check "hostedBuild" "INFO" "not run by default; command=$hostedBuildCommand"
    }

    $hostedCommands = @(
        "gui.start",
        "gui.smoke.launchshadow",
        "desktop.appmodel.summary",
        "desktop.appmodel.typed-dispatch-gate"
    )
    [void]$CommandsRun.Add(".\guideXOSServer.exe < gui.start; gui.smoke.launchshadow; desktop.appmodel.summary; desktop.appmodel.typed-dispatch-gate; exit")
    try {
        $hostedOutput = Invoke-ServerCommands -Commands $hostedCommands
        Add-LogSection "hosted-appmodel-output" $hostedOutput

        if ((Text-Contains $hostedOutput "command: gui.smoke.launchshadow") -and
            (Text-Contains $hostedOutput "mode: diagnostic-only") -and
            (Text-Contains $hostedOutput "launchesApps: false") -and
            (Text-Contains $hostedOutput "runtimeLaunchBehaviorChanged: false")) {
            Add-Check "gui.smoke.launchshadow" "PASS" "diagnostic-only runtimeLaunchBehaviorChanged=false"
        } else {
            Add-Check "gui.smoke.launchshadow" "FAIL" "missing required diagnostic-only launch-shadow markers"
        }

        $summaryOverall = Get-MatchValue $hostedOutput '^overall:\s*(\S+)'
        if ($summaryOverall -eq "OK") {
            Add-Check "desktop.appmodel.summary" "PASS" "overall=OK"
        } elseif ($summaryOverall) {
            Add-Check "desktop.appmodel.summary" "WARN" "overall=$summaryOverall"
        } else {
            Add-Check "desktop.appmodel.summary" "FAIL" "overall line not found"
        }

        $gateStatus = Get-MatchValue $hostedOutput '^gateStatus:\s*(\S+)'
        if ($gateStatus -eq "PASS") {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "PASS" "gateStatus=PASS"
        } elseif ($gateStatus -eq "WARN" -or $gateStatus -eq "NOT-RUN") {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "WARN" "gateStatus=$gateStatus"
        } elseif ($gateStatus) {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "FAIL" "gateStatus=$gateStatus"
        } else {
            Add-Check "desktop.appmodel.typed-dispatch-gate" "FAIL" "gateStatus line not found"
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
            )
            Add-LogSection "typed-dispatch-flag-smoke-output" $flagResult.Output
            if ($flagResult.ExitCode -eq 0 -and (Text-Contains $flagResult.Output "result=PASS")) {
                Add-Check "typedDispatchFlagSmoke" "PASS" "single-flag and invalid-flag diagnostics passed"
            } else {
                Add-Check "typedDispatchFlagSmoke" "FAIL" "exitCode=$($flagResult.ExitCode) or missing result=PASS"
            }
        } catch {
            Add-Check "typedDispatchFlagSmoke" "FAIL" $_.Exception.Message
        }
    }

    $qemuSmokeCommand = ".\scripts\smoke-appmodel-launchshadow.ps1 -TimeoutSeconds $TimeoutSeconds"
    if ($IncludeQemu) {
        [void]$CommandsRun.Add($qemuSmokeCommand)
        try {
            $qemuResult = Invoke-ProcessCommand -Name "qemu-launchshadow" -FilePath "powershell.exe" -ArgumentList @(
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                "`"$(Join-Path $Root "scripts\smoke-appmodel-launchshadow.ps1")`"",
                "-TimeoutSeconds",
                "$TimeoutSeconds"
            )
            Add-LogSection "qemu-launchshadow-smoke-output" $qemuResult.Output
            if ($qemuResult.ExitCode -eq 0 -and (Text-Contains $qemuResult.Output "App-model launch shadow kernel smoke PASS")) {
                Add-Check "qemuLaunchShadowSmoke" "PASS" "command=$qemuSmokeCommand"
            } else {
                Add-Check "qemuLaunchShadowSmoke" "FAIL" "exitCode=$($qemuResult.ExitCode) or missing PASS marker"
            }
        } catch {
            Add-Check "qemuLaunchShadowSmoke" "FAIL" $_.Exception.Message
        }
    } else {
        [void]$CommandsListed.Add("$qemuSmokeCommand (pass -IncludeQemu to run)")
        Add-Check "qemuLaunchShadowSmoke" "INFO" "optional; not run without -IncludeQemu"
    }

    $hasFail = $false
    $hasWarn = $false
    foreach ($check in $Checks) {
        if ($check.Status -eq "FAIL") { $hasFail = $true }
        if ($check.Status -eq "WARN") { $hasWarn = $true }
    }
    $overall = if ($hasFail) { "FAIL" } elseif ($hasWarn) { "WARN" } else { "PASS" }

    $reportLines = @(
        "[AppModelPhase2Status]",
        "mode=validation-report-only",
        "typedDispatchEnabled=false",
        "feedsTypedDispatchIntoLaunch=false",
        "qemuOptional=true",
        "status=$overall",
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
