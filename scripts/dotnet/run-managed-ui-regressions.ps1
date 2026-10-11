[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c169-managed-ui\spill\host'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'out\dotnet')).TrimEnd('\', '/') + '\'
if (-not $EvidenceRoot.StartsWith($allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "C169 host evidence must remain under $allowedRoot"
}
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'

$script:steps = [System.Collections.Generic.List[object]]::new()
function Invoke-C169Step([string]$Name, [scriptblock]$Action, [string]$LogPath) {
    $output = ''
    $exitCode = 1
    try {
        $output = (& $Action 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
    }
    catch {
        $output = ($_ | Out-String)
        $exitCode = 1
    }
    Set-Content -LiteralPath $LogPath -Value $output -Encoding utf8
    $status = if ($exitCode -eq 0) { 'PASS' } else { 'FAIL' }
    $step = [ordered]@{ name = $Name; status = $status; exitCode = $exitCode; log = $LogPath }
    $script:steps.Add($step)
    Write-Host "C169 step $Name=$status exitCode=$exitCode"
    return [pscustomobject]@{ status = $status; exitCode = $exitCode; output = $output; step = $step }
}

$dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$powershell = Get-Command powershell.exe -ErrorAction SilentlyContinue | Select-Object -First 1
$c135Build = $null
$hostGate = $null
$settingsGate = $null

if (-not $dotnet) {
    $c135Build = [pscustomobject]@{ status = 'FAIL'; exitCode = 127; output = 'dotnet.exe was not found.'; step = $null }
    $script:steps.Add([ordered]@{ name = 'C135-focused-host-compilation'; status = 'FAIL'; exitCode = 127; log = '' })
} else {
    $c135BuildRoot = [System.IO.Path]::GetFullPath((Join-Path $EvidenceRoot 'c135-focused-host-build'))
    $c135Project = Join-Path $RepoRoot 'samples\managed\HostLogProof\HostLogProof.csproj'
    $c135Args = @('build', $c135Project, '--configuration', 'Release',
        '-p:HostLogProofMode=C135Composite',
        '-p:HostLogProofC135IncludeFocusedTests=true',
        '-p:PublishAot=false', '-p:SelfContained=false',
        "-p:BaseOutputPath=$(Join-Path $c135BuildRoot 'bin')\",
        "-p:BaseIntermediateOutputPath=$(Join-Path $c135BuildRoot 'obj')\",
        "-p:MSBuildProjectExtensionsPath=$(Join-Path $c135BuildRoot 'obj')\")
    $c135Build = Invoke-C169Step 'C135-focused-host-compilation' {
        & $dotnet.Source @c135Args
    } (Join-Path $EvidenceRoot 'c135-focused-host-build.txt')
}

if (-not $dotnet) {
    $hostGate = [pscustomobject]@{ status = 'FAIL'; exitCode = 127; output = 'dotnet.exe was not found.'; step = $null }
    $script:steps.Add([ordered]@{ name = 'managed-ui-host-suites'; status = 'FAIL'; exitCode = 127; log = '' })
} else {
    $hostProject = Join-Path $RepoRoot 'samples\host\ManagedUiRegression\ManagedUiRegression.csproj'
    $hostGate = Invoke-C169Step 'managed-ui-host-suites' {
        & $dotnet.Source run --project $hostProject --configuration Release `
            --artifacts-path (Join-Path $EvidenceRoot "dotnet-artifacts-$runId")
    } (Join-Path $EvidenceRoot 'managed-ui-host-suites.txt')
}

$settingsRunner = Join-Path $PSScriptRoot 'run-c168-settings-ui-host-tests.ps1'
if (-not $powershell) {
    $settingsGate = [pscustomobject]@{ status = 'FAIL'; exitCode = 127; output = 'powershell.exe was not found.'; step = $null }
    $script:steps.Add([ordered]@{ name = 'c167-c168-settings-host-suites'; status = 'FAIL'; exitCode = 127; log = '' })
} else {
    $settingsArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $settingsRunner,
        '-RepoRoot', $RepoRoot, '-EvidenceRoot', (Join-Path $EvidenceRoot 'c168-settings'),
        '-ArtifactsRoot', (Join-Path $EvidenceRoot "c168-settings\dotnet-artifacts-$runId"))
    $settingsGate = Invoke-C169Step 'c167-c168-settings-host-suites' {
        & $powershell.Source @settingsArgs
    } (Join-Path $EvidenceRoot 'c167-c168-settings-host-suites.txt')
}

$suiteMatches = [regex]::Matches($hostGate.output,
    '(?m)^MANAGED-UI-SUITE name=(\S+) cases=(\d+) result=(PASS|FAIL)(?:\s+exception=(\S+))?\s*$')
$suites = @(
    foreach ($match in $suiteMatches) {
        [ordered]@{
            name = $match.Groups[1].Value
            cases = [int]$match.Groups[2].Value
            result = $match.Groups[3].Value
            exception = if ($match.Groups[4].Success) { $match.Groups[4].Value } else { $null }
        }
    }
)
$summaryMatch = [regex]::Match($hostGate.output,
    '(?m)^MANAGED-UI-HOST-GATE suites=(\d+) passed=(\d+) failed=(\d+) cases=(\d+) result=(PASS|FAIL)\s*$')
$stressMatch = [regex]::Match($hostGate.output,
    '(?m)^C169-STRESS popupCycles=(\d+) scrollViewMovements=(\d+) c168Replays=(\d+) c168ReplayPageCycles=(\d+) c168ReplaySelectionChanges=(\d+) c168ReplayApplyResetCycles=(\d+) c168ReplayPopupCycles=(\d+) settingsCloseReopenCycles=(\d+) result=(PASS|FAIL)\s*$')
$c167Match = [regex]::Match($settingsGate.output,
    'C167 host UI/state cases=(\d+) stress=(\d+) result=(PASS|FAIL)')
$c168Match = [regex]::Match($settingsGate.output,
    'C168 host UI/layout cases=(\d+) pageCycles=(\d+) selectionChanges=(\d+) applyResetCycles=(\d+) popupCycles=(\d+) result=(PASS|FAIL)')

$hostSummaryValid = $summaryMatch.Success -and $summaryMatch.Groups[5].Value -eq 'PASS' -and
    [int]$summaryMatch.Groups[1].Value -eq 14 -and $suites.Count -eq 14 -and
    @($suites | Where-Object result -ne 'PASS').Count -eq 0
$c135Pass = $c135Build.status -eq 'PASS'
$c168Pass = $settingsGate.status -eq 'PASS' -and $c167Match.Success -and
    $c167Match.Groups[3].Value -eq 'PASS' -and $c168Match.Success -and
    $c168Match.Groups[6].Value -eq 'PASS'
$stressPass = $stressMatch.Success -and $stressMatch.Groups[9].Value -eq 'PASS' -and
    [int]$stressMatch.Groups[1].Value -eq 500 -and
    [int]$stressMatch.Groups[2].Value -eq 250 -and
    [int]$stressMatch.Groups[3].Value -eq 100 -and
    [int]$stressMatch.Groups[8].Value -eq 100
$gatePassed = $c135Pass -and $hostGate.status -eq 'PASS' -and
    $hostSummaryValid -and $c168Pass -and $stressPass

$manifest = [ordered]@{
    phase = 'C169-managed-ui-host-regression'
    sourceHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
    runId = $runId
    status = if ($gatePassed) { 'PASS' } else { 'FAIL' }
    noQemuRequired = $true
    c135FocusedHostCompilation = [ordered]@{
        result = $c135Build.status
        buildConfiguration = 'HostLogProofMode=C135Composite, focused tests enabled, host-only compile'
        exitCode = $c135Build.exitCode
        log = $c135Build.step.log
    }
    focusedSuites = $suites
    focusedSummary = if ($summaryMatch.Success) {
        [ordered]@{
            suites = [int]$summaryMatch.Groups[1].Value
            passed = [int]$summaryMatch.Groups[2].Value
            failed = [int]$summaryMatch.Groups[3].Value
            cases = [int]$summaryMatch.Groups[4].Value
            result = $summaryMatch.Groups[5].Value
        }
    } else { $null }
    renderingAndStress = if ($stressMatch.Success) {
        [ordered]@{
            listBoxFocusTransitions = 500
            popupOpenCloseCycles = [int]$stressMatch.Groups[1].Value
            scrollViewMovements = [int]$stressMatch.Groups[2].Value
            c168ReplayInvocations = [int]$stressMatch.Groups[3].Value
            c168ReplayPageCycles = [int]$stressMatch.Groups[4].Value
            c168ReplaySelectionChanges = [int]$stressMatch.Groups[5].Value
            c168ReplayApplyResetCycles = [int]$stressMatch.Groups[6].Value
            c168ReplayPopupCycles = [int]$stressMatch.Groups[7].Value
            settingsCloseReopenCycles = [int]$stressMatch.Groups[8].Value
            result = $stressMatch.Groups[9].Value
        }
    } else { $null }
    c167 = if ($c167Match.Success) {
        [ordered]@{ focusedCases = [int]$c167Match.Groups[1].Value; stressIterations = [int]$c167Match.Groups[2].Value; result = $c167Match.Groups[3].Value }
    } else { [ordered]@{ result = 'FAIL' } }
    c168 = if ($c168Match.Success) {
        [ordered]@{
            focusedCases = [int]$c168Match.Groups[1].Value
            pageEntryExitCycles = [int]$c168Match.Groups[2].Value
            selectionChanges = [int]$c168Match.Groups[3].Value
            applyResetCycles = [int]$c168Match.Groups[4].Value
            popupCycles = [int]$c168Match.Groups[5].Value
            additionalCloseReopenCycles = if ($stressMatch.Success) { [int]$stressMatch.Groups[8].Value } else { 0 }
            result = $c168Match.Groups[6].Value
        }
    } else { [ordered]@{ result = 'FAIL' } }
    steps = $script:steps
}
$manifestPath = Join-Path $EvidenceRoot 'managed-ui-host-manifest.json'
$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "Managed UI Regression Gate result=$($manifest.status) manifest=$manifestPath"
if (-not $gatePassed) { exit 1 }
exit 0
