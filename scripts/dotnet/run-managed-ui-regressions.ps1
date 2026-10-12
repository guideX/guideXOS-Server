[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c170-control-rendering\host'
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
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = (& $Action 2>&1 | Out-String)
        $exitCode = $LASTEXITCODE
    }
    catch {
        $output = ($_ | Out-String)
        $exitCode = 1
    }
    finally {
        $ErrorActionPreference = $savedErrorActionPreference
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
$c170StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C170-STRESS buttonFocusChanges=(\d+) buttonActivations=(\d+) comboFocusChanges=(\d+) dropdownCycles=(\d+) keyboardSelectionChanges=(\d+) pointerSelectionChanges=(\d+) settingsCloseReopenCycles=(\d+) result=(PASS|FAIL)\s*$')
$c167Match = [regex]::Match($settingsGate.output,
    'C167 host UI/state cases=(\d+) stress=(\d+) result=(PASS|FAIL)')
$c168Match = [regex]::Match($settingsGate.output,
    'C168 host UI/layout cases=(\d+) pageCycles=(\d+) selectionChanges=(\d+) applyResetCycles=(\d+) popupCycles=(\d+) result=(PASS|FAIL)')

$baselineSuiteNames = @(
    'C133-ComboBox', 'C134-TransientRouting', 'C135-PopupMenuApi',
    'C135-PopupMenuHost', 'C137-Wheel', 'C139-SharedViewport',
    'C140-ScrollView', 'C141-VerticalStack', 'C142-MarginsAlignment',
    'C145-Dialogs', 'C146-Settings', 'C146-StoreFormat',
    'C169-ListBoxRendering', 'C169-Stress')
$suiteNames = @($suites | ForEach-Object { $_.name })
$hostSummaryValid = $summaryMatch.Success -and $summaryMatch.Groups[5].Value -eq 'PASS' -and
    [int]$summaryMatch.Groups[1].Value -eq 16 -and $suites.Count -eq 16 -and
    [int]$summaryMatch.Groups[2].Value -eq 16 -and
    [int]$summaryMatch.Groups[3].Value -eq 0 -and
    [int]$summaryMatch.Groups[4].Value -eq 560 -and
    @($baselineSuiteNames | Where-Object { $suiteNames -notcontains $_ }).Count -eq 0 -and
    @($suiteNames | Where-Object { $_ -notin $baselineSuiteNames -and
        $_ -notin @('C170-ControlRendering', 'C170-Stress') }).Count -eq 0 -and
    @($suites | Where-Object result -ne 'PASS').Count -eq 0
$renderingSuite = @($suites | Where-Object name -eq 'C170-ControlRendering')
$renderingSuitePass = $renderingSuite.Count -eq 1 -and
    $renderingSuite[0].cases -eq 20 -and $renderingSuite[0].result -eq 'PASS'
$renderingStressSuite = @($suites | Where-Object name -eq 'C170-Stress')
$renderingStressSuitePass = $renderingStressSuite.Count -eq 1 -and
    $renderingStressSuite[0].cases -eq 7 -and
    $renderingStressSuite[0].result -eq 'PASS'
$c135Pass = $c135Build.status -eq 'PASS'
$c168Pass = $settingsGate.status -eq 'PASS' -and $c167Match.Success -and
    $c167Match.Groups[3].Value -eq 'PASS' -and $c168Match.Success -and
    $c168Match.Groups[6].Value -eq 'PASS'
$stressPass = $stressMatch.Success -and $stressMatch.Groups[9].Value -eq 'PASS' -and
    [int]$stressMatch.Groups[1].Value -eq 500 -and
    [int]$stressMatch.Groups[2].Value -eq 250 -and
    [int]$stressMatch.Groups[3].Value -eq 100 -and
    [int]$stressMatch.Groups[8].Value -eq 100
$c170StressPass = $c170StressMatch.Success -and
    [int]$c170StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c170StressMatch.Groups[2].Value -eq 500 -and
    [int]$c170StressMatch.Groups[3].Value -eq 1000 -and
    [int]$c170StressMatch.Groups[4].Value -eq 500 -and
    [int]$c170StressMatch.Groups[5].Value -eq 250 -and
    [int]$c170StressMatch.Groups[6].Value -eq 250 -and
    [int]$c170StressMatch.Groups[7].Value -eq 100 -and
    $renderingSuitePass -and $renderingStressSuitePass
$gatePassed = $c135Pass -and $hostGate.status -eq 'PASS' -and
    $hostSummaryValid -and $c168Pass -and $stressPass -and $c170StressPass

$artifactRecords = @(
    foreach ($step in $script:steps) {
        if ([string]::IsNullOrWhiteSpace([string]$step.log)) { continue }
        $logPath = [System.IO.Path]::GetFullPath([string]$step.log)
        if (Test-Path -LiteralPath $logPath -PathType Leaf) {
            [ordered]@{
                name = [string]$step.name
                path = $logPath
                sha256 = (Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash.ToUpperInvariant()
                sizeBytes = (Get-Item -LiteralPath $logPath).Length
            }
        } else {
            [ordered]@{ name = [string]$step.name; path = $logPath; sha256 = $null; sizeBytes = $null }
        }
    }
)
$integrityErrors = [System.Collections.Generic.List[string]]::new()
if ($artifactRecords.Count -ne $script:steps.Count) {
    $integrityErrors.Add('One or more required step logs have no manifest reference.')
}
foreach ($artifact in $artifactRecords) {
    if (-not (Test-Path -LiteralPath $artifact.path -PathType Leaf)) {
        $integrityErrors.Add("Required log is missing: $($artifact.path)")
        continue
    }
    $actualHash = (Get-FileHash -LiteralPath $artifact.path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actualHash -ne [string]$artifact.sha256) {
        $integrityErrors.Add("Required log hash mismatch: $($artifact.path)")
    }
}
$sourceHead = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($sourceHead -notmatch '^[0-9a-fA-F]{40}$') {
    $integrityErrors.Add('Source HEAD is missing or malformed.')
}
$manifestStatus = if ($gatePassed -and $integrityErrors.Count -eq 0) { 'PASS' } else { 'FAIL' }

$manifest = [ordered]@{
    schemaVersion = 1
    phase = 'C170-managed-control-rendering-host-regression'
    sourceHead = $sourceHead
    runId = $runId
    status = $manifestStatus
    noQemuRequired = $true
    artifactIntegrity = [ordered]@{
        result = if ($integrityErrors.Count -eq 0) { 'PASS' } else { 'FAIL' }
        algorithm = 'SHA-256'
        referencedFiles = $artifactRecords
        errors = @($integrityErrors)
    }
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
    c170ControlRendering = [ordered]@{
        focusedCases = if ($renderingSuite.Count -eq 1) { $renderingSuite[0].cases } else { 0 }
        buttonFocusChanges = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[1].Value } else { 0 }
        buttonActivations = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[2].Value } else { 0 }
        comboFocusChanges = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[3].Value } else { 0 }
        dropdownOpenCloseCycles = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[4].Value } else { 0 }
        keyboardSelectionChanges = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[5].Value } else { 0 }
        pointerSelectionChanges = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[6].Value } else { 0 }
        settingsCloseReopenCycles = if ($c170StressMatch.Success) { [int]$c170StressMatch.Groups[7].Value } else { 0 }
        result = if ($c170StressPass) { 'PASS' } else { 'FAIL' }
    }
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
$manifestJson = $manifest | ConvertTo-Json -Depth 12
Set-Content -LiteralPath $manifestPath -Value $manifestJson -Encoding utf8
$parsedManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -ErrorAction Stop
$manifestValid = $parsedManifest.schemaVersion -eq 1 -and
    $parsedManifest.phase -eq 'C170-managed-control-rendering-host-regression' -and
    $parsedManifest.sourceHead -eq $sourceHead -and
    $parsedManifest.status -eq $manifestStatus -and
    $parsedManifest.artifactIntegrity.referencedFiles.Count -eq $artifactRecords.Count -and
    $parsedManifest.artifactIntegrity.result -eq 'PASS'
if (-not $manifestValid) {
    Write-Error "Generated C170 host manifest failed schema or reference validation: $manifestPath"
}
Write-Host "Managed UI Regression Gate result=$($manifest.status) suites=$($suites.Count) cases=$($summaryMatch.Groups[4].Value) manifest=$manifestPath"
if (-not $gatePassed -or -not $manifestValid) { exit 1 }
exit 0
