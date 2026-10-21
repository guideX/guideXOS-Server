$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$inventoryPath = Join-Path $PSScriptRoot 'managed-ui-regression-inventory.json'
Import-Module (Join-Path $PSScriptRoot 'ManagedUiRegressionInventory.psm1') -Force
$inventory = Get-Content -LiteralPath $inventoryPath -Raw | ConvertFrom-Json
$inventoryHash = (Get-FileHash -LiteralPath $inventoryPath -Algorithm SHA256).Hash.ToUpperInvariant()

function New-PassingManifest {
    $suites = @(
        foreach ($suite in $inventory.suites) {
            [pscustomobject]@{
                name = [string]$suite.name
                cases = [int]$suite.cases
                result = 'PASS'
                exception = $null
            }
        }
    )
    [pscustomobject]@{
        schemaVersion = 1
        status = 'PASS'
        suiteInventoryId = [string]$inventory.inventoryId
        suiteInventorySha256 = $inventoryHash
        focusedSuites = $suites
        focusedSummary = [pscustomobject]@{
            suites = [int]$inventory.expectedSuiteCount
            passed = [int]$inventory.expectedSuiteCount
            failed = 0
            cases = [int]$inventory.expectedCaseCount
            result = 'PASS'
        }
    }
}

function Copy-TestManifest([object] $Manifest) {
    return ($Manifest | ConvertTo-Json -Depth 12 | ConvertFrom-Json)
}

$valid = Test-ManagedUiRegressionManifest -Manifest (New-PassingManifest) `
    -Inventory $inventory -InventorySha256 $inventoryHash
if (-not $valid.Valid) {
    throw "A correct $($inventory.inventoryId) manifest was rejected: $($valid.Errors -join '; ')"
}

$incorrectInputs = [System.Collections.Generic.List[string]]::new()
$wrongIdentity = Copy-TestManifest (New-PassingManifest)
$wrongIdentity.suiteInventoryId = 'arbitrary-inventory'
$incorrectInputs.Add('identity')

$wrongInventoryHash = Copy-TestManifest (New-PassingManifest)
$wrongInventoryHash.suiteInventorySha256 = 'incorrect-inventory-hash'
$incorrectInputs.Add('inventory-hash')

$missingSuite = Copy-TestManifest (New-PassingManifest)
$missingSuite.focusedSuites = @($missingSuite.focusedSuites | Select-Object -Skip 1)
$incorrectInputs.Add('missing-suite')

$wrongCaseCount = Copy-TestManifest (New-PassingManifest)
$wrongCaseCount.focusedSuites[0].cases++
$incorrectInputs.Add('case-count')

$failedSuite = Copy-TestManifest (New-PassingManifest)
$failedSuite.focusedSuites[0].result = 'FAIL'
$incorrectInputs.Add('failed-suite')

$wrongSummary = Copy-TestManifest (New-PassingManifest)
$wrongSummary.focusedSummary.cases--
$incorrectInputs.Add('summary-count')

$rejected = 0
foreach ($bad in @($wrongIdentity, $wrongInventoryHash, $missingSuite,
        $wrongCaseCount, $failedSuite, $wrongSummary)) {
    $result = Test-ManagedUiRegressionManifest -Manifest $bad `
        -Inventory $inventory -InventorySha256 $inventoryHash
    if (-not $result.Valid) { $rejected++ }
}
if ($rejected -ne $incorrectInputs.Count) {
    throw "Inventory validator rejected $rejected of $($incorrectInputs.Count) deliberately incorrect manifests."
}

Write-Output "MANAGED-UI-INVENTORY-TEST id=$($inventory.inventoryId) suites=$($inventory.expectedSuiteCount) cases=$($inventory.expectedCaseCount) validManifest=PASS incorrectManifestsRejected=$rejected result=PASS"
