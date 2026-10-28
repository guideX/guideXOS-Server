[CmdletBinding()]
param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$EvidenceRoot = "",
    [string]$ArtifactsRoot = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$inventoryPath = Join-Path $PSScriptRoot 'managed-ui-regression-inventory.json'
if (-not (Test-Path -LiteralPath $inventoryPath -PathType Leaf)) {
    throw "Required managed UI regression inventory is missing: $inventoryPath"
}
Import-Module (Join-Path $PSScriptRoot 'ManagedUiRegressionInventory.psm1') -Force
$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json -ErrorAction Stop
$inventorySha256 = (Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $EvidenceRoot = Join-Path $RepoRoot 'out\dotnet\c180-managed-resize-contract\host'
}
$EvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceRoot)
if (-not [string]::IsNullOrWhiteSpace($ArtifactsRoot)) {
    $ArtifactsRoot = [System.IO.Path]::GetFullPath($ArtifactsRoot)
} else {
    $ArtifactsRoot = $EvidenceRoot
}
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
            --artifacts-path (Join-Path $ArtifactsRoot "dotnet-artifacts-$runId")
    } (Join-Path $EvidenceRoot 'managed-ui-host-suites.txt')
}

$settingsRunner = Join-Path $PSScriptRoot 'run-c168-settings-ui-host-tests.ps1'
if (-not $powershell) {
    $settingsGate = [pscustomobject]@{ status = 'FAIL'; exitCode = 127; output = 'powershell.exe was not found.'; step = $null }
    $script:steps.Add([ordered]@{ name = 'c167-c168-settings-host-suites'; status = 'FAIL'; exitCode = 127; log = '' })
} else {
    $settingsArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $settingsRunner,
        '-RepoRoot', $RepoRoot, '-EvidenceRoot', (Join-Path $EvidenceRoot 'c168-settings'),
        '-ArtifactsRoot', (Join-Path $ArtifactsRoot "c168-settings\dotnet-artifacts-$runId"))
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
$c171StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C171-STRESS layout=(\d+) geometry=(\d+) focus=(\d+) scroll=(\d+) combo=(\d+) lifecycle=(\d+) allocationBytes=(\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c171StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C171-STRESS-DETAIL generalPageAllocationBytes=(\d+) defaultAppsAttach=(\S+)\s*$')
$c171WindowLayoutDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C171-WINDOW-LAYOUT-DETAIL firstFailedCase=(\d+)\s*$')
$c172StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C172-STRESS geometry=(\d+) transitions=(\d+) focus=(\d+) combo=(\d+) scroll=(\d+) lifecycle=(\d+) allocationBytes=(\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c172StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C172-STRESS-DETAIL firstFailedCase=(\d+) failure=(\S+)\s*$')
$c173WindowResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C173-WINDOW-RESIZE-DETAIL firstFailedCase=(\d+)\s*$')
$c173StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C173-STRESS layoutUpdates=(\d+) alternatingResizes=(\d+) selections=(\d+) scrolls=(\d+) keyboard=(\d+) navigationResize=(\d+) lifecycles=(\d+) allocationBytes=(\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c173StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C173-STRESS-DETAIL failure=(\S+)\s*$')
$c174WindowResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C174-WINDOW-RESIZE-DETAIL firstFailedCase=(\d+)\s*$')
$c174StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C174-STRESS geometry=(\d+) alternating=(\d+) isolation=(\d+) rollback=(\d+) allocationBytes=(\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c175WindowResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C175-NOTES-RESIZE-DETAIL firstFailedCase=(\d+)\s*$')
$c175StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C175-STRESS layout=(\d+) alternating=(\d+) failed=(\d+) rollback=(\d+) lifecycles=(\d+) allocationBytes=(-?\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c176WindowNavigationDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C176-NOTES-NAVIGATION-DETAIL firstFailedCase=(\d+)\s*$')
$c176StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C176-STRESS navigation=(\d+) alternating=(\d+) navigationResize=(\d+) rejected=(\d+) lifecycles=(\d+) allocationBytes=(-?\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c177NewlineDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C177-NOTES-NEWLINE-DETAIL firstFailedCase=(\d+)\s*$')
$c177StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C177-STRESS operations=(\d+) mixedInsertions=(\d+) mixedRemovals=(\d+) capacityRejected=(\d+) alternatingResizes=(\d+) rejectedGeometry=(\d+) lifecycles=(\d+) roundTrips=(\d+) resizeAllocationBytes=(-?\d+) geometryDrift=(\d+) result=(PASS|FAIL)\s*$')
$c178ResizeSuite = @($suites | Where-Object name -eq 'C178-CalculatorResize')
$c178ResizeStressSuite = @($suites | Where-Object name -eq 'C178-CalculatorResizeStress')
$c178ResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C178-CALCULATOR-RESIZE-DETAIL firstFailedCase=([^\s]*)\s*$')
$c178StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C178-STRESS layout=(\d+) alternating=(\d+) arithmetic=(\d+) lifecycles=(\d+) allocationBytes=(-?\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c178StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C178-STRESS-DETAIL firstFailedCase=([^\s]*)\s*$')
$c179ResizeSuite = @($suites | Where-Object name -eq 'C179-TaskManagerResize')
$c179ResizeStressSuite = @($suites | Where-Object name -eq 'C179-TaskManagerResizeStress')
$c179ResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C179-TASK-MANAGER-RESIZE-DETAIL firstFailedCase=([^\s]*)\s*$')
$c179StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C179-STRESS layout=(\d+) alternating=(\d+) refreshResize=(\d+) rejected=(\d+) lifecycles=(\d+) resizeAllocationBytes=(-?\d+) refreshAllocationBytes=(-?\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c179StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C179-STRESS-DETAIL firstFailedCase=([^\s]*)\s*$')
$c180ResizeSuite = @($suites | Where-Object name -eq 'C180-ResizeContract')
$c180ResizeStressSuite = @($suites | Where-Object name -eq 'C180-ResizeContractStress')
$c180ResizeDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C180-RESIZE-CONTRACT-DETAIL firstFailedCase=(\d+)\s*$')
$c180StressMatch = [regex]::Match($hostGate.output,
    '(?m)^C180-STRESS transactions=(\d+) alternating=(\d+) rejected=(\d+) crossApplication=(\d+) lifecycles=(\d+) allocationBytes=(-?\d+) drift=(\d+) result=(PASS|FAIL)\s*$')
$c180StressDetailMatch = [regex]::Match($hostGate.output,
    '(?m)^C180-STRESS-DETAIL ([^\r\n]*)\s*$')
$c167Match = [regex]::Match($settingsGate.output,
    'C167 host UI/state cases=(\d+) stress=(\d+) result=(PASS|FAIL)')
$c168Match = [regex]::Match($settingsGate.output,
    'C168 host UI/layout cases=(\d+) pageCycles=(\d+) selectionChanges=(\d+) applyResetCycles=(\d+) popupCycles=(\d+) result=(PASS|FAIL)')

$focusedSummary = if ($summaryMatch.Success) {
    [ordered]@{
        suites = [int]$summaryMatch.Groups[1].Value
        passed = [int]$summaryMatch.Groups[2].Value
        failed = [int]$summaryMatch.Groups[3].Value
        cases = [int]$summaryMatch.Groups[4].Value
        result = $summaryMatch.Groups[5].Value
    }
} else { $null }
$inventoryProbe = [ordered]@{
    schemaVersion = 1
    status = if ($summaryMatch.Success -and $summaryMatch.Groups[5].Value -eq 'PASS') { 'PASS' } else { 'FAIL' }
    suiteInventoryId = [string]$inventory.inventoryId
    suiteInventorySha256 = $inventorySha256
    focusedSuites = $suites
    focusedSummary = $focusedSummary
}
$inventoryValidation = Test-ManagedUiRegressionManifest -Manifest $inventoryProbe `
    -Inventory $inventory -InventorySha256 $inventorySha256
$hostSummaryValid = $inventoryValidation.Valid
if (-not $hostSummaryValid) {
    Write-Host "Managed UI inventory validation failed: $($inventoryValidation.Errors -join '; ')"
}
$renderingSuite = @($suites | Where-Object name -eq 'C170-ControlRendering')
$renderingSuitePass = $renderingSuite.Count -eq 1 -and
    $renderingSuite[0].cases -eq 21 -and $renderingSuite[0].result -eq 'PASS'
$renderingStressSuite = @($suites | Where-Object name -eq 'C170-Stress')
$renderingStressSuitePass = $renderingStressSuite.Count -eq 1 -and
    $renderingStressSuite[0].cases -eq 7 -and
    $renderingStressSuite[0].result -eq 'PASS'
$windowLayoutSuite = @($suites | Where-Object name -eq 'C171-WindowLayout')
$windowLayoutStressSuite = @($suites | Where-Object name -eq 'C171-Stress')
$windowLayoutSuitePass = $windowLayoutSuite.Count -eq 1 -and
    $windowLayoutSuite[0].cases -eq 35 -and $windowLayoutSuite[0].result -eq 'PASS'
$windowLayoutStressSuitePass = $windowLayoutStressSuite.Count -eq 1 -and
    $windowLayoutStressSuite[0].cases -eq 7 -and
    $windowLayoutStressSuite[0].result -eq 'PASS'
$windowResizeSuite = @($suites | Where-Object name -eq 'C172-WindowResize')
$windowResizeStressSuite = @($suites | Where-Object name -eq 'C172-Stress')
$windowResizeSuitePass = $windowResizeSuite.Count -eq 1 -and
    $windowResizeSuite[0].cases -eq 43 -and
    $windowResizeSuite[0].result -eq 'PASS'
$windowResizeStressSuitePass = $windowResizeStressSuite.Count -eq 1 -and
    $windowResizeStressSuite[0].cases -eq 7 -and
    $windowResizeStressSuite[0].result -eq 'PASS'
$explorerResizeSuite = @($suites | Where-Object name -eq 'C173-ExplorerResize')
$explorerStressSuite = @($suites | Where-Object name -eq 'C173-ExplorerStress')
$explorerResizeSuitePass = $explorerResizeSuite.Count -eq 1 -and
    $explorerResizeSuite[0].cases -eq 47 -and
    $explorerResizeSuite[0].result -eq 'PASS'
$explorerStressSuitePass = $explorerStressSuite.Count -eq 1 -and
    $explorerStressSuite[0].cases -eq 10 -and
    $explorerStressSuite[0].result -eq 'PASS'
$sharedResizeSuite = @($suites | Where-Object name -eq 'C174-SharedResize')
$sharedResizeStressSuite = @($suites | Where-Object name -eq 'C174-Stress')
$sharedResizeSuitePass = $sharedResizeSuite.Count -eq 1 -and
    $sharedResizeSuite[0].cases -eq 22 -and
    $sharedResizeSuite[0].result -eq 'PASS'
$sharedResizeStressSuitePass = $sharedResizeStressSuite.Count -eq 1 -and
    $sharedResizeStressSuite[0].cases -eq 4 -and
    $sharedResizeStressSuite[0].result -eq 'PASS'
$notesResizeSuite = @($suites | Where-Object name -eq 'C175-NotesResize')
$notesResizeStressSuite = @($suites | Where-Object name -eq 'C175-NotesResizeStress')
$notesResizeInventoryEntry = @($inventory.suites | Where-Object name -eq 'C175-NotesResize')
$notesResizeExpectedCases = if ($notesResizeInventoryEntry.Count -eq 1) {
    [int]$notesResizeInventoryEntry[0].cases
} else { -1 }
$notesNavigationSuite = @($suites | Where-Object name -eq 'C176-NotesNavigation')
$notesNavigationStressSuite = @($suites | Where-Object name -eq 'C176-NotesNavigationStress')
$notesNewlineSuite = @($suites | Where-Object name -eq 'C177-NotesNewline')
$notesNewlineStressSuite = @($suites | Where-Object name -eq 'C177-NotesNewlineStress')
$notesResizeSuitePass = $notesResizeSuite.Count -eq 1 -and
    $notesResizeSuite[0].cases -eq $notesResizeExpectedCases -and
    $notesResizeSuite[0].result -eq 'PASS'
$notesResizeStressSuitePass = $notesResizeStressSuite.Count -eq 1 -and
    $notesResizeStressSuite[0].cases -eq 7 -and $notesResizeStressSuite[0].result -eq 'PASS'
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
$c171StressPass = $c171StressMatch.Success -and
    $c171StressDetailMatch.Success -and
    [int]$c171StressDetailMatch.Groups[1].Value -eq 0 -and
    $c171StressDetailMatch.Groups[2].Value -eq 'valid' -and
    $c171WindowLayoutDetailMatch.Success -and
    [int]$c171WindowLayoutDetailMatch.Groups[1].Value -eq 0 -and
    $c171StressMatch.Groups[9].Value -eq 'PASS' -and
    [int]$c171StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c171StressMatch.Groups[2].Value -eq 500 -and
    [int]$c171StressMatch.Groups[3].Value -eq 500 -and
    [int]$c171StressMatch.Groups[4].Value -eq 250 -and
    [int]$c171StressMatch.Groups[5].Value -eq 250 -and
    [int]$c171StressMatch.Groups[6].Value -eq 100 -and
    [int]$c171StressMatch.Groups[7].Value -eq 0 -and
    [int]$c171StressMatch.Groups[8].Value -eq 0 -and
    $windowLayoutSuitePass -and $windowLayoutStressSuitePass
$c172StressPass = $c172StressMatch.Success -and
    $c172StressDetailMatch.Success -and
    [int]$c172StressDetailMatch.Groups[1].Value -eq 0 -and
    $c172StressDetailMatch.Groups[2].Value -eq 'none' -and
    $c172StressMatch.Groups[9].Value -eq 'PASS' -and
    [int]$c172StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c172StressMatch.Groups[2].Value -eq 500 -and
    [int]$c172StressMatch.Groups[3].Value -eq 500 -and
    [int]$c172StressMatch.Groups[4].Value -eq 250 -and
    [int]$c172StressMatch.Groups[5].Value -eq 250 -and
    [int]$c172StressMatch.Groups[6].Value -eq 100 -and
    [int]$c172StressMatch.Groups[7].Value -le 4096 -and
    [int]$c172StressMatch.Groups[8].Value -eq 0 -and
    $windowResizeSuitePass -and $windowResizeStressSuitePass
$c173StressPass = $c173StressMatch.Success -and
    $c173StressDetailMatch.Success -and
    $c173WindowResizeDetailMatch.Success -and
    [int]$c173WindowResizeDetailMatch.Groups[1].Value -eq 0 -and
    $c173StressDetailMatch.Groups[1].Value -eq 'none' -and
    $c173StressMatch.Groups[10].Value -eq 'PASS' -and
    [int]$c173StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c173StressMatch.Groups[2].Value -eq 500 -and
    [int]$c173StressMatch.Groups[3].Value -eq 500 -and
    [int]$c173StressMatch.Groups[4].Value -eq 250 -and
    [int]$c173StressMatch.Groups[5].Value -eq 250 -and
    [int]$c173StressMatch.Groups[6].Value -eq 100 -and
    [int]$c173StressMatch.Groups[7].Value -eq 100 -and
    [int]$c173StressMatch.Groups[8].Value -eq 0 -and
    [int]$c173StressMatch.Groups[9].Value -eq 0 -and
    $explorerResizeSuitePass -and $explorerStressSuitePass
$c174StressPass = $c174StressMatch.Success -and
    $c174WindowResizeDetailMatch.Success -and
    [int]$c174WindowResizeDetailMatch.Groups[1].Value -eq 0 -and
    $c174StressMatch.Groups[7].Value -eq 'PASS' -and
    [int]$c174StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c174StressMatch.Groups[2].Value -eq 500 -and
    [int]$c174StressMatch.Groups[3].Value -eq 500 -and
    [int]$c174StressMatch.Groups[4].Value -eq 500 -and
    [int]$c174StressMatch.Groups[5].Value -eq 0 -and
    [int]$c174StressMatch.Groups[6].Value -eq 0 -and
    $sharedResizeSuitePass -and $sharedResizeStressSuitePass
$c175StressPass = $c175StressMatch.Success -and
    $c175WindowResizeDetailMatch.Success -and
    [int]$c175WindowResizeDetailMatch.Groups[1].Value -eq 0 -and
    $c175StressMatch.Groups[8].Value -eq 'PASS' -and
    [int]$c175StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c175StressMatch.Groups[2].Value -eq 500 -and
    [int]$c175StressMatch.Groups[3].Value -eq 250 -and
    [int]$c175StressMatch.Groups[4].Value -eq 250 -and
    [int]$c175StressMatch.Groups[5].Value -eq 100 -and
    [int]$c175StressMatch.Groups[6].Value -eq 0 -and
    [int]$c175StressMatch.Groups[7].Value -eq 0 -and
    $notesResizeSuitePass -and $notesResizeStressSuitePass
$c176NavigationSuitePass = $notesNavigationSuite.Count -eq 1 -and
    $notesNavigationSuite[0].cases -eq 20 -and
    $notesNavigationSuite[0].result -eq 'PASS'
$c176NavigationStressSuitePass = $notesNavigationStressSuite.Count -eq 1 -and
    $notesNavigationStressSuite[0].cases -eq 7 -and
    $notesNavigationStressSuite[0].result -eq 'PASS'
$c176StressPass = $c176StressMatch.Success -and
    $c176WindowNavigationDetailMatch.Success -and
    [int]$c176WindowNavigationDetailMatch.Groups[1].Value -eq 0 -and
    $c176StressMatch.Groups[8].Value -eq 'PASS' -and
    [int]$c176StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c176StressMatch.Groups[2].Value -eq 500 -and
    [int]$c176StressMatch.Groups[3].Value -eq 250 -and
    [int]$c176StressMatch.Groups[4].Value -eq 250 -and
    [int]$c176StressMatch.Groups[5].Value -eq 100 -and
    [int]$c176StressMatch.Groups[6].Value -eq 0 -and
    [int]$c176StressMatch.Groups[7].Value -eq 0 -and
    $c176NavigationSuitePass -and $c176NavigationStressSuitePass
$c177NewlineSuitePass = $notesNewlineSuite.Count -eq 1 -and
    $notesNewlineSuite[0].cases -eq 23 -and
    $notesNewlineSuite[0].result -eq 'PASS'
$c177NewlineStressSuitePass = $notesNewlineStressSuite.Count -eq 1 -and
    $notesNewlineStressSuite[0].cases -eq 8 -and
    $notesNewlineStressSuite[0].result -eq 'PASS'
$c177StressPass = $c177StressMatch.Success -and
    $c177NewlineDetailMatch.Success -and
    [int]$c177NewlineDetailMatch.Groups[1].Value -eq 0 -and
    $c177StressMatch.Groups[11].Value -eq 'PASS' -and
    [int]$c177StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c177StressMatch.Groups[2].Value -eq 250 -and
    [int]$c177StressMatch.Groups[3].Value -eq 250 -and
    [int]$c177StressMatch.Groups[4].Value -eq 250 -and
    [int]$c177StressMatch.Groups[5].Value -eq 500 -and
    [int]$c177StressMatch.Groups[6].Value -eq 250 -and
    [int]$c177StressMatch.Groups[7].Value -eq 100 -and
    [int]$c177StressMatch.Groups[8].Value -eq 100 -and
    [int]$c177StressMatch.Groups[9].Value -eq 0 -and
    [int]$c177StressMatch.Groups[10].Value -eq 0 -and
    $c177NewlineSuitePass -and $c177NewlineStressSuitePass
$c178ResizeSuitePass = $c178ResizeSuite.Count -eq 1 -and
    $c178ResizeSuite[0].cases -eq 18 -and
    $c178ResizeSuite[0].result -eq 'PASS'
$c178ResizeStressSuitePass = $c178ResizeStressSuite.Count -eq 1 -and
    $c178ResizeStressSuite[0].cases -eq 5 -and
    $c178ResizeStressSuite[0].result -eq 'PASS'
$c178StressPass = $c178StressMatch.Success -and
    $c178ResizeDetailMatch.Success -and $c178ResizeDetailMatch.Groups[1].Value -eq '' -and
    $c178StressDetailMatch.Success -and $c178StressDetailMatch.Groups[1].Value -eq '' -and
    $c178StressMatch.Groups[7].Value -eq 'PASS' -and
    [int]$c178StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c178StressMatch.Groups[2].Value -eq 500 -and
    [int]$c178StressMatch.Groups[3].Value -eq 250 -and
    [int]$c178StressMatch.Groups[4].Value -eq 100 -and
    [int]$c178StressMatch.Groups[5].Value -eq 0 -and
    [int]$c178StressMatch.Groups[6].Value -eq 0 -and
    $c178ResizeSuitePass -and $c178ResizeStressSuitePass
$c179ResizeSuitePass = $c179ResizeSuite.Count -eq 1 -and
    $c179ResizeSuite[0].cases -eq 20 -and
    $c179ResizeSuite[0].result -eq 'PASS'
$c179ResizeStressSuitePass = $c179ResizeStressSuite.Count -eq 1 -and
    $c179ResizeStressSuite[0].cases -eq 5 -and
    $c179ResizeStressSuite[0].result -eq 'PASS'
$c179StressPass = $c179StressMatch.Success -and
    $c179ResizeDetailMatch.Success -and $c179ResizeDetailMatch.Groups[1].Value -eq '' -and
    $c179StressDetailMatch.Success -and $c179StressDetailMatch.Groups[1].Value -eq '' -and
    $c179StressMatch.Groups[9].Value -eq 'PASS' -and
    [int]$c179StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c179StressMatch.Groups[2].Value -eq 500 -and
    [int]$c179StressMatch.Groups[3].Value -eq 250 -and
    [int]$c179StressMatch.Groups[4].Value -eq 250 -and
    [int]$c179StressMatch.Groups[5].Value -eq 100 -and
    [long]$c179StressMatch.Groups[6].Value -eq 0 -and
    [int]$c179StressMatch.Groups[8].Value -eq 0 -and
    $c179ResizeSuitePass -and $c179ResizeStressSuitePass
$c180ResizeSuitePass = $c180ResizeSuite.Count -eq 1 -and
    $c180ResizeSuite[0].cases -eq 21 -and
    $c180ResizeSuite[0].result -eq 'PASS'
$c180ResizeStressSuitePass = $c180ResizeStressSuite.Count -eq 1 -and
    $c180ResizeStressSuite[0].cases -eq 7 -and
    $c180ResizeStressSuite[0].result -eq 'PASS'
$c180StressPass = $c180StressMatch.Success -and
    $c180ResizeDetailMatch.Success -and
    [int]$c180ResizeDetailMatch.Groups[1].Value -eq 0 -and
    $c180StressDetailMatch.Success -and
    $c180StressDetailMatch.Groups[1].Value -eq '' -and
    $c180StressMatch.Groups[8].Value -eq 'PASS' -and
    [int]$c180StressMatch.Groups[1].Value -eq 1000 -and
    [int]$c180StressMatch.Groups[2].Value -eq 500 -and
    [int]$c180StressMatch.Groups[3].Value -eq 250 -and
    [int]$c180StressMatch.Groups[4].Value -eq 250 -and
    [int]$c180StressMatch.Groups[5].Value -eq 100 -and
    [long]$c180StressMatch.Groups[6].Value -eq 0 -and
    [int]$c180StressMatch.Groups[7].Value -eq 0 -and
    $c180ResizeSuitePass -and $c180ResizeStressSuitePass
$gatePassed = $c135Pass -and $hostGate.status -eq 'PASS' -and
    $hostSummaryValid -and $c168Pass -and $stressPass -and $c170StressPass -and
    $c171StressPass -and $c172StressPass -and $c173StressPass -and
    $c174StressPass -and $c175StressPass -and $c176StressPass -and
    $c177StressPass -and $c178StressPass -and $c179StressPass -and
    $c180StressPass

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
    phase = 'managed-ui-regression-aggregate'
    sourceHead = $sourceHead
    runId = $runId
    status = $manifestStatus
    noQemuRequired = $true
    suiteInventoryId = [string]$inventory.inventoryId
    suiteInventorySha256 = $inventorySha256
    suiteInventoryExpectedSuiteCount = [int]$inventory.expectedSuiteCount
    suiteInventoryExpectedCaseCount = [int]$inventory.expectedCaseCount
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
    focusedSummary = $focusedSummary
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
    c171ManagedWindowLayout = [ordered]@{
        focusedCases = if ($windowLayoutSuite.Count -eq 1) { $windowLayoutSuite[0].cases } else { 0 }
        layoutRecomputations = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[1].Value } else { 0 }
        alternatingGeometryChanges = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[2].Value } else { 0 }
        focusTransitions = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[3].Value } else { 0 }
        scrollViewMovements = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[4].Value } else { 0 }
        comboCycles = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[5].Value } else { 0 }
        lifecycleIterations = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[6].Value } else { 0 }
        hotPathAllocationBytes = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[7].Value } else { -1 }
        generalPageHotPathAllocationBytes = if ($c171StressDetailMatch.Success) { [int]$c171StressDetailMatch.Groups[1].Value } else { -1 }
        defaultAppsAttachValidation = if ($c171StressDetailMatch.Success) { $c171StressDetailMatch.Groups[2].Value } else { 'missing' }
        firstFailedCase = if ($c171WindowLayoutDetailMatch.Success) { [int]$c171WindowLayoutDetailMatch.Groups[1].Value } else { -1 }
        driftCount = if ($c171StressMatch.Success) { [int]$c171StressMatch.Groups[8].Value } else { -1 }
        result = if ($c171StressPass) { 'PASS' } else { 'FAIL' }
    }
    c172ManagedWindowResize = [ordered]@{
        focusedCases = if ($windowResizeSuite.Count -eq 1) { $windowResizeSuite[0].cases } else { 0 }
        runtimeGeometryUpdates = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[1].Value } else { 0 }
        alternatingTransitions = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[2].Value } else { 0 }
        focusTransitions = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[3].Value } else { 0 }
        comboCycles = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[4].Value } else { 0 }
        scrollViewMovements = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[5].Value } else { 0 }
        lifecycleIterations = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[6].Value } else { 0 }
        hotPathAllocationBytes = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[7].Value } else { -1 }
        driftCount = if ($c172StressMatch.Success) { [int]$c172StressMatch.Groups[8].Value } else { -1 }
        firstFailedCase = if ($c172StressDetailMatch.Success) { [int]$c172StressDetailMatch.Groups[1].Value } else { -1 }
        failureDetail = if ($c172StressDetailMatch.Success) { $c172StressDetailMatch.Groups[2].Value } else { 'missing' }
        result = if ($c172StressPass) { 'PASS' } else { 'FAIL' }
    }
    c173ManagedFileExplorerResize = [ordered]@{
        focusedCases = if ($explorerResizeSuite.Count -eq 1) { $explorerResizeSuite[0].cases } else { 0 }
        layoutUpdates = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[1].Value } else { 0 }
        alternatingResizes = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[2].Value } else { 0 }
        selectionTransitions = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[3].Value } else { 0 }
        scrollOperations = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[4].Value } else { 0 }
        keyboardNavigations = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[5].Value } else { 0 }
        navigationResizeOperations = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[6].Value } else { 0 }
        lifecycleCycles = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[7].Value } else { 0 }
        hotPathAllocationBytes = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[8].Value } else { -1 }
        driftCount = if ($c173StressMatch.Success) { [int]$c173StressMatch.Groups[9].Value } else { -1 }
        firstFailedCase = if ($c173WindowResizeDetailMatch.Success) { [int]$c173WindowResizeDetailMatch.Groups[1].Value } else { -1 }
        failureDetail = if ($c173StressDetailMatch.Success) { $c173StressDetailMatch.Groups[1].Value } else { 'missing' }
        result = if ($c173StressPass) { 'PASS' } else { 'FAIL' }
    }
    c174SharedManagedResize = [ordered]@{
        focusedCases = if ($sharedResizeSuite.Count -eq 1) { $sharedResizeSuite[0].cases } else { 0 }
        geometryTransitions = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[1].Value } else { 0 }
        alternatingCycles = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[2].Value } else { 0 }
        independentWindowTransitions = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[3].Value } else { 0 }
        rollbackChecks = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[4].Value } else { 0 }
        hotPathAllocationBytes = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[5].Value } else { -1 }
        driftCount = if ($c174StressMatch.Success) { [int]$c174StressMatch.Groups[6].Value } else { -1 }
        firstFailedCase = if ($c174WindowResizeDetailMatch.Success) { [int]$c174WindowResizeDetailMatch.Groups[1].Value } else { -1 }
        result = if ($c174StressPass) { 'PASS' } else { 'FAIL' }
    }
    c175ManagedNotesResize = [ordered]@{
        focusedCases = if ($notesResizeSuite.Count -eq 1) { $notesResizeSuite[0].cases } else { 0 }
        layoutUpdates = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[1].Value } else { 0 }
        alternatingResizes = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[2].Value } else { 0 }
        rejectedGeometryRequests = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[3].Value } else { 0 }
        rollbackChecks = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[4].Value } else { 0 }
        lifecycleCycles = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[5].Value } else { 0 }
        hotPathAllocationBytes = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[6].Value } else { -1 }
        driftCount = if ($c175StressMatch.Success) { [int]$c175StressMatch.Groups[7].Value } else { -1 }
        firstFailedCase = if ($c175WindowResizeDetailMatch.Success) { [int]$c175WindowResizeDetailMatch.Groups[1].Value } else { -1 }
        result = if ($c175StressPass) { 'PASS' } else { 'FAIL' }
    }
    c176ManagedNotesNavigation = [ordered]@{
        focusedCases = if ($notesNavigationSuite.Count -eq 1) { $notesNavigationSuite[0].cases } else { 0 }
        navigationOperations = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[1].Value } else { 0 }
        alternatingResizes = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[2].Value } else { 0 }
        navigationResizeOperations = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[3].Value } else { 0 }
        rejectedGeometryRequests = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[4].Value } else { 0 }
        lifecycleCycles = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[5].Value } else { 0 }
        hotPathAllocationBytes = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[6].Value } else { -1 }
        driftCount = if ($c176StressMatch.Success) { [int]$c176StressMatch.Groups[7].Value } else { -1 }
        firstFailedCase = if ($c176WindowNavigationDetailMatch.Success) { [int]$c176WindowNavigationDetailMatch.Groups[1].Value } else { -1 }
        result = if ($c176StressPass) { 'PASS' } else { 'FAIL' }
    }
    c177ManagedNotesNewlineSave = [ordered]@{
        focusedCases = if ($notesNewlineSuite.Count -eq 1) { $notesNewlineSuite[0].cases } else { 0 }
        editingNavigationOperations = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[1].Value } else { 0 }
        mixedDelimiterInsertions = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[2].Value } else { 0 }
        mixedDelimiterRemovals = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[3].Value } else { 0 }
        capacityRejections = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[4].Value } else { 0 }
        alternatingResizes = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[5].Value } else { 0 }
        rejectedGeometryRequests = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[6].Value } else { 0 }
        lifecycleCycles = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[7].Value } else { 0 }
        serializationRoundTrips = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[8].Value } else { 0 }
        resizeAllocationBytes = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[9].Value } else { -1 }
        geometryDriftCount = if ($c177StressMatch.Success) { [int]$c177StressMatch.Groups[10].Value } else { -1 }
        firstFailedCase = if ($c177NewlineDetailMatch.Success) { [int]$c177NewlineDetailMatch.Groups[1].Value } else { -1 }
        result = if ($c177StressPass) { 'PASS' } else { 'FAIL' }
    }
    c178ManagedCalculatorResize = [ordered]@{
        focusedCases = if ($c178ResizeSuite.Count -eq 1) { $c178ResizeSuite[0].cases } else { 0 }
        stressCases = if ($c178ResizeStressSuite.Count -eq 1) { $c178ResizeStressSuite[0].cases } else { 0 }
        layoutUpdates = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[1].Value } else { 0 }
        alternatingResizes = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[2].Value } else { 0 }
        arithmeticResizeCycles = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[3].Value } else { 0 }
        lifecycleCycles = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[4].Value } else { 0 }
        hotPathAllocationBytes = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[5].Value } else { -1 }
        driftCount = if ($c178StressMatch.Success) { [int]$c178StressMatch.Groups[6].Value } else { -1 }
        firstFailedCase = if ($c178ResizeDetailMatch.Success) { $c178ResizeDetailMatch.Groups[1].Value } else { 'missing' }
        stressFirstFailedCase = if ($c178StressDetailMatch.Success) { $c178StressDetailMatch.Groups[1].Value } else { 'missing' }
        result = if ($c178StressPass) { 'PASS' } else { 'FAIL' }
    }
    c179ManagedTaskManagerResize = [ordered]@{
        focusedCases = if ($c179ResizeSuite.Count -eq 1) { $c179ResizeSuite[0].cases } else { 0 }
        stressCases = if ($c179ResizeStressSuite.Count -eq 1) { $c179ResizeStressSuite[0].cases } else { 0 }
        layoutUpdates = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[1].Value } else { 0 }
        alternatingResizes = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[2].Value } else { 0 }
        refreshResizeCycles = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[3].Value } else { 0 }
        rejectedGeometryChecks = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[4].Value } else { 0 }
        lifecycleCycles = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[5].Value } else { 0 }
        resizeAllocationBytes = if ($c179StressMatch.Success) { [long]$c179StressMatch.Groups[6].Value } else { -1 }
        refreshAllocationBytes = if ($c179StressMatch.Success) { [long]$c179StressMatch.Groups[7].Value } else { -1 }
        geometryDriftCount = if ($c179StressMatch.Success) { [int]$c179StressMatch.Groups[8].Value } else { -1 }
        firstFailedCase = if ($c179ResizeDetailMatch.Success) { $c179ResizeDetailMatch.Groups[1].Value } else { 'missing' }
        stressFirstFailedCase = if ($c179StressDetailMatch.Success) { $c179StressDetailMatch.Groups[1].Value } else { 'missing' }
        result = if ($c179StressPass) { 'PASS' } else { 'FAIL' }
    }
    c180ManagedResizeContract = [ordered]@{
        focusedCases = if ($c180ResizeSuite.Count -eq 1) { $c180ResizeSuite[0].cases } else { 0 }
        stressCases = if ($c180ResizeStressSuite.Count -eq 1) { $c180ResizeStressSuite[0].cases } else { 0 }
        sharedTransactions = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[1].Value } else { 0 }
        alternatingRequests = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[2].Value } else { 0 }
        rejectedGeometryRequests = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[3].Value } else { 0 }
        crossApplicationStateCombinations = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[4].Value } else { 0 }
        lifecycleCycles = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[5].Value } else { 0 }
        hotPathAllocationBytes = if ($c180StressMatch.Success) { [long]$c180StressMatch.Groups[6].Value } else { -1 }
        geometryDriftCount = if ($c180StressMatch.Success) { [int]$c180StressMatch.Groups[7].Value } else { -1 }
        firstFailedCase = if ($c180ResizeDetailMatch.Success) { [int]$c180ResizeDetailMatch.Groups[1].Value } else { -1 }
        stressFailureDetail = if ($c180StressDetailMatch.Success) { $c180StressDetailMatch.Groups[1].Value } else { 'missing' }
        result = if ($c180StressPass) { 'PASS' } else { 'FAIL' }
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
$manifestInventoryValidation = Test-ManagedUiRegressionManifest `
    -Manifest $parsedManifest -Inventory $inventory -InventorySha256 $inventorySha256
$manifestValid = $parsedManifest.schemaVersion -eq 1 -and
    $parsedManifest.phase -eq 'managed-ui-regression-aggregate' -and
    $parsedManifest.sourceHead -eq $sourceHead -and
    $parsedManifest.status -eq $manifestStatus -and
    $parsedManifest.artifactIntegrity.referencedFiles.Count -eq $artifactRecords.Count -and
    $parsedManifest.artifactIntegrity.result -eq 'PASS' -and
    $manifestInventoryValidation.Valid
if (-not $manifestValid) {
    Write-Error "Generated managed UI aggregate manifest failed schema, inventory, or reference validation: $manifestPath"
}
Write-Host "Managed UI Regression Gate result=$($manifest.status) suites=$($suites.Count) cases=$($focusedSummary.cases) inventory=$($inventory.inventoryId) manifest=$manifestPath"
if (-not $gatePassed -or -not $manifestValid) { exit 1 }
exit 0
