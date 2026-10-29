function Get-C188R12R2R1SettingsDispatchEvidence(
    [string]$SerialText,
    [bool]$RequireLifecycleTests = $true) {
    $events = [System.Collections.Generic.List[object]]::new()
    $malformed = [System.Collections.Generic.List[object]]::new()
    $lineNumber = 0
    $linePattern = '^\[C102-MANAGED-OUTPUT\] C188R12R2_LAUNCH stage=(?<stage>[a-z-]+) appId=com\.guidexos\.apps\.managed\.settingscenter selector=5 generation=(?<generation>\d+) result=(?<result>BEGIN|PASS|FAIL)(?: code=(?<code>-?\d+))?$'

    foreach ($line in ($SerialText -split "`r?`n")) {
        $lineNumber++
        if ($line -notmatch '^\[C102-MANAGED-OUTPUT\] C188R12R2_LAUNCH\b') {
            continue
        }
        $match = [regex]::Match($line, $linePattern)
        if (-not $match.Success) {
            $malformed.Add([ordered]@{ line=$lineNumber; text=$line })
            continue
        }
        $events.Add([ordered]@{
            line=$lineNumber
            stage=$match.Groups['stage'].Value
            generation=[uint64]$match.Groups['generation'].Value
            result=$match.Groups['result'].Value
            code=if ($match.Groups['code'].Success) { [int]$match.Groups['code'].Value } else { $null }
            text=$line
        })
    }

    $rank = @{
        'dispatch-context' = 0
        'dispatch-prelude' = 1
        'registration-lookup:BEGIN' = 2
        'registration-lookup:PASS' = 3
        'registration-lookup:FAIL' = 3
        'runtime-settings-init:BEGIN' = 4
        'runtime-settings-init:PASS' = 5
        'runtime-settings-init:FAIL' = 5
        'managed-lifecycle-tests:BEGIN' = 6
        'managed-lifecycle-tests:PASS' = 7
        'managed-lifecycle-tests:FAIL' = 7
        'lifetime-start:BEGIN' = 8
        'lifetime-start:PASS' = 9
        'lifetime-start:FAIL' = 9
        'application-entry' = 10
        'application-return:PASS' = 11
        'application-return:FAIL' = 11
        'dispatch-return:PASS' = 12
        'dispatch-return:FAIL' = 12
    }
    $stages = @{}
    $lastRank = -1
    $orderingValid = $malformed.Count -eq 0
    $orderingErrors = [System.Collections.Generic.List[string]]::new()
    $generation = $null
    $firstFailure = $null
    foreach ($event in $events) {
        if ($null -ne $firstFailure -and
            ($event.stage -ne 'dispatch-return' -or $event.result -ne 'FAIL')) {
            $orderingValid = $false
            $orderingErrors.Add("continued-after-explicit-failure:$($event.stage):$($event.result)")
        }
        $key = if ($event.stage -in @('dispatch-context','dispatch-prelude','application-entry')) {
            $event.stage
        } else {
            "$($event.stage):$($event.result)"
        }
        if (-not $rank.ContainsKey($key)) {
            $orderingValid = $false
            $orderingErrors.Add("unknown-stage:$key")
            continue
        }
        if ($rank[$key] -le $lastRank) {
            $orderingValid = $false
            $orderingErrors.Add("out-of-order-or-duplicate:$key at line $($event.line)")
        }
        $lastRank = $rank[$key]
        if ($null -eq $generation) { $generation = [uint64]$event.generation }
        elseif ([uint64]$event.generation -ne $generation) {
            $orderingValid = $false
            $orderingErrors.Add("generation-changed:$($event.generation) at line $($event.line)")
        }
        if (-not $stages.ContainsKey([string]$event.stage)) {
            $stages[[string]$event.stage] = [System.Collections.Generic.List[object]]::new()
        }
        $stages[[string]$event.stage].Add($event)
        if ($event.result -eq 'FAIL' -and $null -eq $firstFailure) {
            $firstFailure = $event
        }
        if ($event.result -eq 'PASS' -and $null -ne $event.code -and [int]$event.code -ne 0) {
            $orderingValid = $false
            $orderingErrors.Add("pass-with-nonzero-code:$key code=$($event.code)")
        }
        if ($event.result -eq 'FAIL' -and ($null -eq $event.code -or [int]$event.code -eq 0)) {
            $orderingValid = $false
            $orderingErrors.Add("fail-without-nonzero-code:$key")
        }
        if ($event.result -eq 'BEGIN' -and $null -ne $event.code) {
            $orderingValid = $false
            $orderingErrors.Add("begin-with-code:$key")
        }
    }

    $hasStageResult = {
        param([string]$Stage,[string]$Result)
        return $stages.ContainsKey($Stage) -and
            @($stages[$Stage] | Where-Object { $_.result -eq $Result }).Count -eq 1
    }
    $hasBegin = {
        param([string]$Stage)
        return & $hasStageResult $Stage 'BEGIN'
    }
    $hasPass = {
        param([string]$Stage)
        return & $hasStageResult $Stage 'PASS'
    }
    $hasFail = {
        param([string]$Stage)
        return & $hasStageResult $Stage 'FAIL'
    }

    $dispatchReturned = (& $hasPass 'dispatch-return') -or (& $hasFail 'dispatch-return')
    $lifecycleObserved = $stages.ContainsKey('managed-lifecycle-tests')
    $firstUnobservedBoundary = $null
    if ($null -ne $firstFailure) {
        if (-not $dispatchReturned) { $firstUnobservedBoundary = 'dispatch-return' }
    } else {
        if (-not (& $hasPass 'dispatch-context')) { $firstUnobservedBoundary = 'dispatch-context' }
        elseif (-not (& $hasPass 'dispatch-prelude')) { $firstUnobservedBoundary = 'dispatch-prelude' }
        elseif (-not (& $hasBegin 'registration-lookup')) { $firstUnobservedBoundary = 'registration-lookup-entry' }
        elseif (-not ((& $hasPass 'registration-lookup') -or (& $hasFail 'registration-lookup'))) { $firstUnobservedBoundary = 'registration-lookup-result' }
        elseif (-not (& $hasBegin 'runtime-settings-init')) { $firstUnobservedBoundary = 'runtime-settings-init-entry' }
        elseif (-not ((& $hasPass 'runtime-settings-init') -or (& $hasFail 'runtime-settings-init'))) { $firstUnobservedBoundary = 'runtime-settings-init-result' }
        elseif ($RequireLifecycleTests -and -not (& $hasBegin 'managed-lifecycle-tests')) { $firstUnobservedBoundary = 'managed-lifecycle-tests-entry' }
        elseif ($RequireLifecycleTests -and -not ((& $hasPass 'managed-lifecycle-tests') -or (& $hasFail 'managed-lifecycle-tests'))) { $firstUnobservedBoundary = 'managed-lifecycle-tests-result' }
        elseif (-not (& $hasBegin 'lifetime-start')) { $firstUnobservedBoundary = 'lifetime-start-entry' }
        elseif (-not ((& $hasPass 'lifetime-start') -or (& $hasFail 'lifetime-start'))) { $firstUnobservedBoundary = 'lifetime-start-result' }
        elseif (-not (& $hasBegin 'application-entry')) { $firstUnobservedBoundary = 'application-entry' }
        elseif (-not ((& $hasPass 'application-return') -or (& $hasFail 'application-return'))) { $firstUnobservedBoundary = 'application-return' }
        elseif (-not ((& $hasPass 'dispatch-return') -or (& $hasFail 'dispatch-return'))) { $firstUnobservedBoundary = 'dispatch-return' }
    }

    $dispatchSucceeded = $orderingValid -and
        (& $hasPass 'dispatch-context') -and (& $hasPass 'dispatch-prelude') -and
        (& $hasBegin 'registration-lookup') -and (& $hasPass 'registration-lookup') -and
        (& $hasBegin 'runtime-settings-init') -and (& $hasPass 'runtime-settings-init') -and
        (-not $RequireLifecycleTests -or ((& $hasBegin 'managed-lifecycle-tests') -and (& $hasPass 'managed-lifecycle-tests'))) -and
        (& $hasBegin 'lifetime-start') -and (& $hasPass 'lifetime-start') -and
        (& $hasBegin 'application-entry') -and (& $hasPass 'application-return') -and
        (& $hasPass 'dispatch-return') -and $null -eq $firstUnobservedBoundary
    $outcome = if (-not $orderingValid) { 'INVALID_MARKERS' }
        elseif ($dispatchSucceeded) { 'DISPATCH_SUCCEEDED' }
        elseif ($null -ne $firstFailure) { 'EXPLICIT_FAILURE' }
        else { 'INCOMPLETE' }
    $lastEvent = if ($events.Count -gt 0) { $events[$events.Count - 1] } else { $null }

    return [ordered]@{
        outcome=$outcome
        markerCount=$events.Count
        malformedMarkerCount=$malformed.Count
        malformedMarkers=@($malformed)
        generation=$generation
        lifecycleMarkerObserved=$lifecycleObserved
        lifecycleMarkerRequired=$RequireLifecycleTests
        markerOrderingValid=$orderingValid
        orderingErrors=@($orderingErrors)
        firstUnobservedBoundary=$firstUnobservedBoundary
        lastObservedStage=if ($null -ne $lastEvent) { [string]$lastEvent.stage } else { $null }
        dispatchReturned=[bool]$dispatchReturned
        firstExplicitFailure=if ($null -ne $firstFailure) { $firstFailure } else { $null }
        events=@($events)
    }
}
