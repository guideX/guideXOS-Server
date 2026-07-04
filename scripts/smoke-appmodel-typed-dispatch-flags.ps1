param(
    [switch]$SkipNormalCheck
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildBat = Join-Path $Root "build.bat"
$OutDir = Join-Path $Root "out\appmodel-typed-dispatch-flags"
$LogDir = Join-Path $Root "logs"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SmokeLog = Join-Path $LogDir "appmodel-typed-dispatch-flags-smoke-$stamp.log"

if (-not (Test-Path $BuildBat)) {
    throw "build.bat not found: $BuildBat"
}

function ConvertTo-BatchPath {
    param([string]$Path)
    return $Path.Replace((Join-Path $Root ""), "")
}

function New-TemporaryBuild {
    param(
        [string]$CaseName,
        [string[]]$Defines
    )

    $tempBuildBat = Join-Path $OutDir "build-$CaseName.bat"
    $exeRelative = "out\appmodel-typed-dispatch-flags\guideXOSServer.$CaseName.exe"
    $exe = Join-Path $Root $exeRelative
    $defineFlags = ($Defines | ForEach-Object { "-D$_" }) -join " "

    $buildText = Get-Content $BuildBat -Raw
    if ($defineFlags.Length -gt 0) {
        $buildText = $buildText -replace 'set CXXFLAGS=([^\r\n]*)', "set CXXFLAGS=`$1 $defineFlags"
    }
    $buildText = $buildText -replace 'set OUTPUT=guideXOSServer\.exe', "set OUTPUT=$exeRelative"
    Set-Content -Path $tempBuildBat -Value $buildText -Encoding ASCII

    return @{
        BuildScript = $tempBuildBat
        Exe = $exe
    }
}

function Invoke-ServerCommands {
    param(
        [string]$ExePath,
        [string[]]$Commands
    )

    if (-not (Test-Path $ExePath)) {
        throw "Executable not found: $ExePath"
    }

    $inputText = (($Commands + @("exit")) -join [Environment]::NewLine) + [Environment]::NewLine
    return $inputText | & $ExePath
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

function Invoke-FlagCase {
    param(
        [string]$CaseName,
        [string[]]$Defines,
        [string]$SummaryLine,
        [string]$GateStatus,
        [string]$GateDetail
    )

    $build = New-TemporaryBuild -CaseName $CaseName -Defines $Defines
    Write-Host "Building temporary typed-dispatch flag diagnostic binary: $CaseName"
    & cmd.exe /c "`"$($build.BuildScript)`""
    if ($LASTEXITCODE -ne 0) {
        throw "Temporary flag build failed for ${CaseName} with exit code $LASTEXITCODE"
    }

    Write-Host "Running typed-dispatch flag diagnostics: $CaseName"
    $output = Invoke-ServerCommands -ExePath $build.Exe -Commands @(
        "desktop.appmodel.summary",
        "desktop.appmodel.typed-dispatch-gate"
    )

    Assert-Contains $output $SummaryLine "$CaseName summary flag line"
    Assert-Contains $output "check=typedDispatchCompileFlags status=$GateStatus" "$CaseName gate flag status"
    Assert-Contains $output $GateDetail "$CaseName gate flag detail"
    Assert-Contains $output "enablesTypedDispatch: true" "$CaseName typed-ready dispatch is enabled"
    Assert-Contains $output "feedsTypedDispatchIntoLaunch: true" "$CaseName typed-ready dispatch feeds launch"

    return [pscustomobject]@{
        CaseName = $CaseName
        BuildScript = $build.BuildScript
        Exe = $build.Exe
        SummaryLine = $SummaryLine
        GateStatus = $GateStatus
        Output = $output
    }
}

Push-Location $Root
try {
    $cases = @(
        @{
            CaseName = "shadow-only"
            Defines = @("GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY")
            SummaryLine = "typedDispatchFlags: shadowOnly=ON enabled=OFF behavior=typed-ready-dispatch status=OK discoveryOnly=false"
            GateStatus = "PASS"
            GateDetail = "shadowOnly=ON enabled=OFF behavior=typed-ready-dispatch discoveryOnly=false"
        },
        @{
            CaseName = "enabled-only"
            Defines = @("GXOS_APPMODEL_TYPED_DISPATCH_ENABLED")
            SummaryLine = "typedDispatchFlags: shadowOnly=OFF enabled=ON behavior=typed-ready-dispatch status=OK discoveryOnly=false"
            GateStatus = "PASS"
            GateDetail = "shadowOnly=OFF enabled=ON behavior=typed-ready-dispatch discoveryOnly=false"
        },
        @{
            CaseName = "both-flags"
            Defines = @("GXOS_APPMODEL_TYPED_DISPATCH_SHADOW_ONLY", "GXOS_APPMODEL_TYPED_DISPATCH_ENABLED")
            SummaryLine = "typedDispatchFlags: shadowOnly=ON enabled=ON behavior=typed-ready-dispatch status=WARN discoveryOnly=false invalidConfiguration=true"
            GateStatus = "WARN"
            GateDetail = "shadowOnly=ON enabled=ON behavior=typed-ready-dispatch discoveryOnly=false invalidConfiguration=true"
        }
    )

    $results = New-Object System.Collections.Generic.List[object]
    foreach ($case in $cases) {
        [void]$results.Add((Invoke-FlagCase @case))
    }

    $normalOutput = ""
    if (-not $SkipNormalCheck) {
        $NormalExe = Join-Path $Root "guideXOSServer.exe"
        Write-Host "Checking normal hosted binary still reports default flags..."
        $normalOutput = Invoke-ServerCommands -ExePath $NormalExe -Commands @(
            "desktop.appmodel.summary",
            "desktop.appmodel.typed-dispatch-gate"
        )
        Assert-Contains $normalOutput "typedDispatchFlags: shadowOnly=OFF enabled=OFF behavior=typed-ready-dispatch status=OK discoveryOnly=false" "normal default flag line"
        # Phase 3 pilot flags must be OFF in default build
        Assert-Contains $normalOutput "appModelPhase3PilotStartMenuNotepadFlag=OFF" "Phase 3 pilot StartMenuNotepad flag default-off"
        Assert-Contains $normalOutput "appModelPhase3PilotFallbackToLegacyFlag=OFF" "Phase 3 pilot FallbackToLegacy flag default-off"
        Assert-Contains $normalOutput "appModelPhase3PilotEnabled=true" "Phase 3 ready-only dispatch enabled=true"
        Assert-Contains $normalOutput "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=true" "Phase 3 ready-only dispatch feeds launch"
        Assert-Contains $normalOutput "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false" "Phase 3 pilot does not change runtime launch behavior"
        Assert-Contains $normalOutput "appModelPhase3PilotDefaultBuildSafe=true" "Phase 3 pilot default-build-safe"
        Assert-Contains $normalOutput "appModelActiveDispatchFeatureGate=appmodel.active-typed-dispatch" "Phase 3A active dispatch feature gate name"
        Assert-Contains $normalOutput "appModelActiveDispatchEnabled=false" "Phase 3A active dispatch default-off"
        Assert-Contains $normalOutput "appModelActiveDispatchRuntimePath=inactive" "Phase 3A active dispatch inactive by default"
    }

    $reportLines = @(
        "[AppModelTypedDispatchFlagsSmoke]",
        "mode=diagnostic-only",
        "temporaryOutputDir=$OutDir",
        "permanentBuildFlagsChanged=false"
    )
    foreach ($result in $results) {
        $reportLines += "case=$($result.CaseName) summaryLine=$($result.SummaryLine)"
        $reportLines += "case=$($result.CaseName) gateCheck=check=typedDispatchCompileFlags status=$($result.GateStatus)"
        $reportLines += "case=$($result.CaseName) launchBehavior=typed-ready-dispatch enablesTypedDispatch=true feedsTypedDispatchIntoLaunch=true"
    }
    $reportLines += "normalDefaultChecked=$((-not $SkipNormalCheck).ToString().ToLowerInvariant())"
    $reportLines += "normalDefaultLine=typedDispatchFlags: shadowOnly=OFF enabled=OFF behavior=typed-ready-dispatch status=OK discoveryOnly=false"
    $reportLines += "appModelPhase3PilotCandidate=StartMenuNotepad"
    $reportLines += "appModelPhase3PilotStartMenuNotepadFlag=OFF"
    $reportLines += "appModelPhase3PilotFallbackToLegacyFlag=OFF"
    $reportLines += "appModelPhase3PilotEnabled=true"
    $reportLines += "appModelPhase3PilotFeedsTypedDispatchIntoLaunch=true"
    $reportLines += "appModelPhase3PilotRuntimeLaunchBehaviorChanged=false"
    $reportLines += "appModelPhase3PilotScopedToStartMenuNotepad=false"
    $reportLines += "appModelPhase3PilotDefaultBuildSafe=true"
    $reportLines += "appModelActiveDispatchFeatureGate=appmodel.active-typed-dispatch"
    $reportLines += "appModelActiveDispatchEnabled=false"
    $reportLines += "appModelActiveDispatchRuntimePath=inactive"
    $reportLines += "result=PASS"
    $report = $reportLines -join [Environment]::NewLine

    $logParts = @($report)
    foreach ($result in $results) {
        $logParts += ""
        $logParts += "[$($result.CaseName)-output]"
        $logParts += ($result.Output -join [Environment]::NewLine)
    }
    $logParts += ""
    $logParts += "[normal-output]"
    $logParts += ($normalOutput -join [Environment]::NewLine)
    Set-Content -Path $SmokeLog -Value ($logParts -join [Environment]::NewLine) -Encoding ASCII

    Write-Output $report
    Write-Host "Smoke log: $SmokeLog"
    exit 0
} finally {
    Pop-Location
}
