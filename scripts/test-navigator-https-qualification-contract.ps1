$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'navigator_https_qualification_contract.ps1')

$requiredCases = @(
    'policy_validated',
    'policy_validated_redirect',
    'policy_validated_wrong_host',
    'policy_validated_downgrade'
)

function New-OutcomeFixture {
    param(
        [string]$Active = 'enabled',
        [string[]]$Enabled = @('yes', 'yes', 'yes', 'yes'),
        [string[]]$Results = @('PASS', 'PASS', 'PASS', 'PASS'),
        [int]$Executed = 4,
        [int]$Passed = 4,
        [int]$Failed = 0,
        [int]$Blocked = 0,
        [int]$Skipped = 0,
        [string]$SuiteResult = 'PASS'
    )

    $lines = @(
        '[NAVIGATOR-SMOKE] BEGIN',
        "[NAVIGATOR-SMOKE] http.active_cases=$Active"
    )
    for ($index = 0; $index -lt $requiredCases.Count; $index++) {
        $name = $requiredCases[$index]
        $lines += "[NAVIGATOR-SMOKE] https.case.$name.enabled=$($Enabled[$index])"
        $lines += "[NAVIGATOR-SMOKE] https.case.$name.result=$($Results[$index])"
    }
    $lines += @(
        '[NAVIGATOR-SMOKE] https.required_cases.total=4',
        "[NAVIGATOR-SMOKE] https.required_cases.executed=$Executed",
        "[NAVIGATOR-SMOKE] https.required_cases.passed=$Passed",
        "[NAVIGATOR-SMOKE] https.required_cases.failed=$Failed",
        "[NAVIGATOR-SMOKE] https.required_cases.blocked=$Blocked",
        "[NAVIGATOR-SMOKE] https.required_cases.skipped=$Skipped",
        "[NAVIGATOR-SMOKE] result=$SuiteResult"
    )
    return $lines -join "`n"
}

$cases = @(
    [pscustomobject]@{
        Name = 'not-run'; Attempted = $false; Output = ''; AssertionsPassed = $false; Expected = 'NOT_RUN'
    },
    [pscustomobject]@{
        Name = 'inactive-kernel'; Attempted = $true
        Output = '[NAVIGATOR-SMOKE] http.active_cases=skipped'; AssertionsPassed = $false; Expected = 'BLOCKED'
    },
    [pscustomobject]@{
        Name = 'trust-prerequisite-disabled'; Attempted = $true
        Output = New-OutcomeFixture -Enabled @('no', 'no', 'no', 'no') -Executed 0 -Passed 0 -Blocked 4
        AssertionsPassed = $false; Expected = 'BLOCKED'
    },
    [pscustomobject]@{
        Name = 'all-required-https-cases-pass'; Attempted = $true
        Output = New-OutcomeFixture; AssertionsPassed = $true; Expected = 'PASS'
    },
    [pscustomobject]@{
        Name = 'required-https-case-fails'; Attempted = $true
        Output = New-OutcomeFixture -Results @('FAIL', 'PASS', 'PASS', 'PASS') -Executed 4 -Passed 3 -Failed 1 -SuiteResult 'FAIL'
        AssertionsPassed = $false; Expected = 'FAIL'
    },
    [pscustomobject]@{
        Name = 'required-count-evidence-missing'; Attempted = $true
        Output = '[NAVIGATOR-SMOKE] http.active_cases=enabled'; AssertionsPassed = $true; Expected = 'BLOCKED'
    },
    [pscustomobject]@{
        Name = 'qac-admission-blocked'; Attempted = $true; Output = ''
        BlockReason = 'QEMU exclusive admission blocked: another qualification owns the QAC mutex.'
        AssertionsPassed = $false; Expected = 'BLOCKED'
    }
)

$failures = 0
foreach ($case in $cases) {
    $result = Get-NavigatorHttpsQualificationOutcome `
        -Attempted $case.Attempted `
        -Output $case.Output `
        -RequiredCaseNames $requiredCases `
        -AssertionsPassed $case.AssertionsPassed `
        -BlockedReason $(if ($case.PSObject.Properties.Match('BlockReason').Count -gt 0) { $case.BlockReason } else { '' })
    if ($result.Outcome -eq $case.Expected) {
        Write-Output "[HOSTED TEST PASS] $($case.Name): outcome=$($result.Outcome) executed=$($result.Executed) passed=$($result.Passed) failed=$($result.Failed) blocked=$($result.Blocked) skipped=$($result.Skipped)"
    } else {
        $failures++
        Write-Output "[HOSTED TEST FAIL] $($case.Name): expected=$($case.Expected) actual=$($result.Outcome) reason=$($result.Reason)"
    }
}

if ($failures -gt 0) {
    throw "Navigator HTTPS qualification contract tests failed: $failures of $($cases.Count)."
}
Write-Output "Navigator HTTPS qualification contract tests PASS: $($cases.Count) cases."
