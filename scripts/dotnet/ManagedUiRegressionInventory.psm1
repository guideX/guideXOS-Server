Set-StrictMode -Version Latest

function Get-ManifestValue {
    param(
        [Parameter(Mandatory = $true)] [object] $Object,
        [Parameter(Mandatory = $true)] [string] $Name
    )

    if ($Object -is [System.Collections.IDictionary]) {
        return $Object[$Name]
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Test-ManagedUiRegressionManifest {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)] [object] $Manifest,
        [Parameter(Mandatory = $true)] [object] $Inventory,
        [Parameter(Mandatory = $true)] [string] $InventorySha256
    )

    $errors = [System.Collections.Generic.List[string]]::new()
    if ((Get-ManifestValue $Inventory 'schemaVersion') -ne 1) {
        $errors.Add('Unsupported inventory schema version.')
    }
    $inventorySuites = @(Get-ManifestValue $Inventory 'suites')
    $expectedSuiteCount = [int](Get-ManifestValue $Inventory 'expectedSuiteCount')
    $expectedCaseCount = [int](Get-ManifestValue $Inventory 'expectedCaseCount')
    $inventoryId = [string](Get-ManifestValue $Inventory 'inventoryId')

    if ([string]::IsNullOrWhiteSpace($inventoryId) -or
        $inventorySuites.Count -ne $expectedSuiteCount) {
        $errors.Add('Inventory identity or suite count is invalid.')
    }
    $inventoryCaseCount = 0
    $expectedNames = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($expected in $inventorySuites) {
        $name = [string](Get-ManifestValue $expected 'name')
        $cases = [int](Get-ManifestValue $expected 'cases')
        if ([string]::IsNullOrWhiteSpace($name) -or $cases -le 0 -or
            -not $expectedNames.Add($name)) {
            $errors.Add("Inventory suite entry is invalid: $name")
        }
        $inventoryCaseCount += $cases
    }
    if ($inventoryCaseCount -ne $expectedCaseCount) {
        $errors.Add('Inventory case total does not match its declared identity.')
    }

    if ([int](Get-ManifestValue $Manifest 'schemaVersion') -ne 1) {
        $errors.Add('Manifest schema version is invalid.')
    }
    if ([string](Get-ManifestValue $Manifest 'suiteInventoryId') -cne $inventoryId) {
        $errors.Add('Manifest inventory identity does not match the accepted baseline.')
    }
    if ([string](Get-ManifestValue $Manifest 'suiteInventorySha256') -cne
        $InventorySha256.ToUpperInvariant()) {
        $errors.Add('Manifest inventory hash does not match the authoritative inventory.')
    }

    $actualSuites = @(Get-ManifestValue $Manifest 'focusedSuites')
    if ($actualSuites.Count -ne $expectedSuiteCount) {
        $errors.Add("Expected $expectedSuiteCount suites; found $($actualSuites.Count).")
    }
    $actualByName = [System.Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::Ordinal)
    foreach ($actual in $actualSuites) {
        $name = [string](Get-ManifestValue $actual 'name')
        if ([string]::IsNullOrWhiteSpace($name) -or $actualByName.ContainsKey($name)) {
            $errors.Add("Manifest has an empty or duplicate suite name: $name")
            continue
        }
        $actualByName.Add($name, $actual)
    }

    $actualCaseCount = 0
    foreach ($expected in $inventorySuites) {
        $name = [string](Get-ManifestValue $expected 'name')
        $expectedCases = [int](Get-ManifestValue $expected 'cases')
        if (-not $actualByName.ContainsKey($name)) {
            $errors.Add("Required suite is missing: $name")
            continue
        }
        $actual = $actualByName[$name]
        $actualCases = [int](Get-ManifestValue $actual 'cases')
        $actualCaseCount += $actualCases
        if ($actualCases -ne $expectedCases) {
            $errors.Add("Suite $name has $actualCases cases; expected $expectedCases.")
        }
        if ([string](Get-ManifestValue $actual 'result') -cne 'PASS') {
            $errors.Add("Suite $name did not pass.")
        }
        $exception = Get-ManifestValue $actual 'exception'
        if ($null -ne $exception -and -not [string]::IsNullOrWhiteSpace([string]$exception)) {
            $errors.Add("Suite $name reported an exception.")
        }
    }
    foreach ($actualName in $actualByName.Keys) {
        if (-not $expectedNames.Contains($actualName)) {
            $errors.Add("Unexpected suite is present: $actualName")
        }
    }
    if ($actualCaseCount -ne $expectedCaseCount) {
        $errors.Add("Expected $expectedCaseCount total cases; found $actualCaseCount.")
    }

    $summary = Get-ManifestValue $Manifest 'focusedSummary'
    if ($null -eq $summary -or
        [int](Get-ManifestValue $summary 'suites') -ne $expectedSuiteCount -or
        [int](Get-ManifestValue $summary 'passed') -ne $expectedSuiteCount -or
        [int](Get-ManifestValue $summary 'failed') -ne 0 -or
        [int](Get-ManifestValue $summary 'cases') -ne $expectedCaseCount -or
        [string](Get-ManifestValue $summary 'result') -cne 'PASS') {
        $errors.Add('Aggregate host summary does not match the accepted inventory.')
    }
    if ([string](Get-ManifestValue $Manifest 'status') -cne 'PASS') {
        $errors.Add('Aggregate manifest status is not PASS.')
    }

    return [pscustomobject]@{
        Valid = ($errors.Count -eq 0)
        Errors = @($errors)
        InventoryId = $inventoryId
        SuiteCount = $actualSuites.Count
        CaseCount = $actualCaseCount
    }
}

Export-ModuleMember -Function Test-ManagedUiRegressionManifest
