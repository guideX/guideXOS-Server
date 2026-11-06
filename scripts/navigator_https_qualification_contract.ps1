function Get-NavigatorHttpsQualificationMarker {
    param(
        [Parameter(Mandatory = $true)][string]$Output,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $pattern = '(?m)^\[NAVIGATOR-SMOKE\] ' + [regex]::Escape($Name) + '=(?<value>[^\r\n]+)\s*$'
    $match = [regex]::Match($Output, $pattern)
    if (-not $match.Success) { return $null }
    return $match.Groups['value'].Value.Trim()
}

function Get-NavigatorHttpsQualificationOutcome {
    param(
        [Parameter(Mandatory = $true)][bool]$Attempted,
        [AllowEmptyString()][string]$Output = '',
        [Parameter(Mandatory = $true)][string[]]$RequiredCaseNames,
        [Parameter(Mandatory = $true)][bool]$AssertionsPassed,
        [AllowEmptyString()][string]$BlockedReason = ''
    )

    $requiredCount = $RequiredCaseNames.Count
    if (-not $Attempted) {
        return [pscustomobject]@{
            Outcome = 'NOT_RUN'; Executed = 0; Passed = 0; Failed = 0
            Blocked = 0; Skipped = $requiredCount; Reason = 'Qualification was not attempted.'
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($BlockedReason)) {
        return [pscustomobject]@{
            Outcome = 'BLOCKED'; Executed = 0; Passed = 0; Failed = 0
            Blocked = $requiredCount; Skipped = 0; Reason = $BlockedReason.Trim()
        }
    }

    $normalizedOutput = $Output -replace '\[KERNEL-HEARTBEAT\][^\r\n]*(?:\r?\n)?', ''
    $activeCases = Get-NavigatorHttpsQualificationMarker -Output $normalizedOutput -Name 'http.active_cases'
    if ($activeCases -eq 'skipped') {
        return [pscustomobject]@{
            Outcome = 'BLOCKED'; Executed = 0; Passed = 0; Failed = 0
            Blocked = $requiredCount; Skipped = 0; Reason = 'Selected kernel reports HTTP smoke cases skipped.'
        }
    }

    if ($activeCases -ne 'enabled') {
        return [pscustomobject]@{
            Outcome = 'BLOCKED'; Executed = 0; Passed = 0; Failed = 0
            Blocked = $requiredCount; Skipped = 0; Reason = 'Serial evidence does not confirm active HTTP smoke cases.'
        }
    }

    $reported = @{}
    foreach ($field in @('total', 'executed', 'passed', 'failed', 'blocked', 'skipped')) {
        $value = Get-NavigatorHttpsQualificationMarker -Output $normalizedOutput -Name "https.required_cases.$field"
        if ($null -eq $value -or $value -notmatch '^\d+$') {
            return [pscustomobject]@{
                Outcome = 'BLOCKED'; Executed = 0; Passed = 0; Failed = 0
                Blocked = $requiredCount; Skipped = 0; Reason = "Missing explicit required HTTPS case count: $field."
            }
        }
        $reported[$field] = [int]$value
    }

    if ($reported.total -ne $requiredCount -or
        $reported.executed -gt $requiredCount -or
        ($reported.passed + $reported.failed) -ne $reported.executed -or
        ($reported.executed + $reported.blocked + $reported.skipped) -ne $reported.total) {
        return [pscustomobject]@{
            Outcome = 'BLOCKED'; Executed = 0; Passed = 0; Failed = 0
            Blocked = $requiredCount; Skipped = 0; Reason = 'Required HTTPS case counters are inconsistent.'
        }
    }

    $caseExecuted = 0
    $casePassed = 0
    $caseFailed = 0
    foreach ($caseName in $RequiredCaseNames) {
        $enabled = Get-NavigatorHttpsQualificationMarker -Output $normalizedOutput -Name "https.case.$caseName.enabled"
        $result = Get-NavigatorHttpsQualificationMarker -Output $normalizedOutput -Name "https.case.$caseName.result"
        if ($enabled -eq 'yes' -and $result -in @('PASS', 'FAIL')) {
            $caseExecuted++
            if ($result -eq 'PASS') { $casePassed++ } else { $caseFailed++ }
        }
    }

    if ($caseExecuted -ne $reported.executed -or
        $casePassed -ne $reported.passed -or
        $caseFailed -ne $reported.failed) {
        return [pscustomobject]@{
            Outcome = 'BLOCKED'; Executed = $caseExecuted; Passed = $casePassed; Failed = $caseFailed
            Blocked = $requiredCount - $caseExecuted; Skipped = $reported.skipped
            Reason = 'Required case result markers do not match the explicit counters.'
        }
    }

    $outcome = if ($caseExecuted -eq 0) {
        'BLOCKED'
    } elseif ($caseExecuted -ne $requiredCount -or $caseFailed -gt 0 -or
        $reported.blocked -gt 0 -or $reported.skipped -gt 0 -or -not $AssertionsPassed) {
        'FAIL'
    } else {
        'PASS'
    }

    $reason = switch ($outcome) {
        'PASS' { 'All required HTTPS cases executed and passed with suite assertions passing.' }
        'FAIL' { 'Required HTTPS cases ran but one or more required assertions failed or were incomplete.' }
        default { 'No required HTTPS case executed; qualification was blocked.' }
    }

    return [pscustomobject]@{
        Outcome = $outcome
        Executed = $caseExecuted
        Passed = $casePassed
        Failed = $caseFailed
        Blocked = $reported.blocked
        Skipped = $reported.skipped
        Reason = $reason
    }
}
