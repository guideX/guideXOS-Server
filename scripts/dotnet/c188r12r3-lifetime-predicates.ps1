function Get-C188R12R3LifetimeEvidence(
    [string]$SerialText,
    [ValidateSet('complete','incomplete','in-progress')]
    [string]$CaptureState = 'incomplete') {
    $lifetimePattern = '^\[C102-MANAGED-OUTPUT\] C188R12R3_LIFETIME invocation=(?<invocation>\d+) stage=(?<stage>[a-z-]+) selector=(?<selector>\d+) identity=(?<identity>\S+) count=(?<count>\d+) mask=(?<mask>\d+) generation=(?<generation>\d+) code=(?<code>-?\d+) state=(?<state>\S+)\r?$'
    $r2Pattern = '^\[C102-MANAGED-OUTPUT\] C188R12R2_LAUNCH stage=(?<stage>[a-z-]+) appId=com\.guidexos\.apps\.managed\.settingscenter selector=5 generation=(?<generation>\d+) result=(?<result>BEGIN|PASS|FAIL)(?: code=(?<code>-?\d+))?\r?$'
    $events = [System.Collections.Generic.List[object]]::new()
    $r2Events = [System.Collections.Generic.List[object]]::new()
    $malformed = 0
    $serialIndex = 0
    foreach ($line in ($SerialText -split "`r?`n")) {
        $serialIndex++
        if ($line -match 'C188R12R3_LIFETIME\b') {
            $match = [regex]::Match($line, $lifetimePattern)
            if (-not $match.Success) { $malformed++; continue }
            $events.Add([pscustomobject]@{
                invocation=[uint32]$match.Groups['invocation'].Value
                stage=$match.Groups['stage'].Value
                selector=[uint32]$match.Groups['selector'].Value
                identity=$match.Groups['identity'].Value
                count=[uint32]$match.Groups['count'].Value
                mask=[uint32]$match.Groups['mask'].Value
                generation=[uint32]$match.Groups['generation'].Value
                code=[int]$match.Groups['code'].Value
                state=$match.Groups['state'].Value
                serialIndex=$serialIndex
            })
        }
        if ($line -match 'C188R12R2_LAUNCH\b') {
            $match = [regex]::Match($line, $r2Pattern)
            if ($match.Success) {
                $r2Events.Add([pscustomobject]@{
                    stage=$match.Groups['stage'].Value
                    result=$match.Groups['result'].Value
                    code=if ($match.Groups['code'].Success) { [int]$match.Groups['code'].Value } else { $null }
                    serialIndex=$serialIndex
                })
            }
        }
    }

    $settingsEvents = @($events | Where-Object {
        $_.selector -eq 5 -and $_.identity -eq 'com.guidexos.apps.managed.settingscenter'
    })
    $selected = @()
    $invocation = $null
    $orderingValid = $true
    $stage = @{}
    $rawOutcome = 'MISSING_SETTINGS_LIFETIME_MARKER'
    $earliest = 'settings-lifetime-entry'
    $callerContinued = $false
    $surfaceObserved = $SerialText -match '(?m)^\[C150-SURFACE\] action=create appId=com\.guidexos\.apps\.managed\.settingscenter generation=[0-9A-Fa-f]{8} window=[0-9A-Fa-f]{8} result=PASS\r?$'

    if ($settingsEvents.Count -gt 0) {
        $invocation = $settingsEvents[-1].invocation
        $selected = @($settingsEvents | Where-Object invocation -eq $invocation)
        $stageRanks = @{
            'enter'=0; 'existing-resident'=1; 'capacity-check'=2; 'generation-check'=3
            'factory-resolve'=4; 'factory-enter'=5; 'factory-return'=6; 'factory-throw'=6
            'resident-register-enter'=7; 'teardown-enter'=8; 'teardown-return'=9
            'teardown-throw'=9; 'resident-register-return'=10; 'owner-assign'=11
            'exit-success'=12; 'exit-failure'=12
        }
        $lastRank = -1
        foreach ($event in $selected) {
            if (-not $stageRanks.ContainsKey($event.stage) -or
                $stage.ContainsKey($event.stage) -or
                $stageRanks[$event.stage] -lt $lastRank) {
                $orderingValid = $false
                break
            }
            $lastRank = $stageRanks[$event.stage]
            $stage[$event.stage] = $event
        }

        $outer = @($r2Events | Where-Object { $_.stage -in @('lifetime-start','application-entry','application-return','dispatch-return') })
        $lifetimeResult = @($outer | Where-Object stage -eq 'lifetime-start' | Select-Object -Last 1)
        $applicationEntry = @($outer | Where-Object stage -eq 'application-entry' | Select-Object -Last 1)
        $applicationReturn = @($outer | Where-Object stage -eq 'application-return' | Select-Object -Last 1)
        $dispatchReturn = @($outer | Where-Object stage -eq 'dispatch-return' | Select-Object -Last 1)
        $callerContinued = $applicationEntry.Count -gt 0

        if ($malformed -gt 0 -or -not $orderingValid) {
            $rawOutcome = 'INVALID_MARKERS'
            $earliest = $null
        } elseif ($stage.ContainsKey('factory-return') -and
            ($stage['factory-return'].code -ne 0 -or $stage['factory-return'].state -eq 'null')) {
            $rawOutcome = 'FACTORY_RETURNED_ERROR'
            $earliest = 'factory-return'
        } elseif ($stage.ContainsKey('factory-throw')) {
            $rawOutcome = 'FACTORY_THROWN'
            $earliest = 'factory-invocation'
        } elseif ($stage.ContainsKey('exit-failure')) {
            $rawOutcome = 'TRY_START_FAILED'
            $earliest = $stage['exit-failure'].state
        } elseif ($stage.ContainsKey('factory-enter') -and
            -not $stage.ContainsKey('factory-return') -and
            -not $stage.ContainsKey('factory-throw')) {
            $rawOutcome = 'FACTORY_ENTERED_NO_RETURN'
            $earliest = 'factory-invocation'
        } elseif ($stage.ContainsKey('factory-return') -and
            -not $stage.ContainsKey('resident-register-return')) {
            $rawOutcome = 'FACTORY_SUCCEEDED_REGISTRATION_INCOMPLETE'
            $earliest = 'resident-registration'
        } elseif ($stage.ContainsKey('resident-register-return') -and
            -not $stage.ContainsKey('owner-assign')) {
            $rawOutcome = 'REGISTRATION_SUCCEEDED_FOLLOWUP_INCOMPLETE'
            $earliest = 'generation-commit'
        } elseif ($stage.ContainsKey('owner-assign') -and
            -not $stage.ContainsKey('exit-success')) {
            $rawOutcome = 'REGISTRATION_SUCCEEDED_FOLLOWUP_INCOMPLETE'
            $earliest = 'try-start-return'
        } elseif ($stage.ContainsKey('exit-success')) {
            if ($lifetimeResult.Count -gt 0 -and $lifetimeResult[0].result -eq 'PASS' -and
                -not $callerContinued) {
                $rawOutcome = 'TRY_START_RETURNED_CALLER_NOT_CONTINUED'
                $earliest = 'application-entry'
            } elseif (-not $callerContinued) {
                $rawOutcome = 'TRY_START_EXIT_OBSERVED_CALLER_RESULT_UNOBSERVED'
                $earliest = 'caller-result'
            } elseif ($applicationReturn.Count -eq 0) {
                $rawOutcome = 'SETTINGS_APPLICATION_ENTRY_NO_RETURN'
                $earliest = 'application-return'
            } elseif ($applicationReturn[0].result -ne 'PASS') {
                $rawOutcome = 'SETTINGS_APPLICATION_RETURNED_ERROR'
                $earliest = 'application-return'
            } elseif ($dispatchReturn.Count -eq 0 -or $dispatchReturn[0].result -ne 'PASS') {
                $rawOutcome = 'SETTINGS_DISPATCH_RETURN_UNOBSERVED'
                $earliest = 'dispatch-return'
            } elseif (-not $surfaceObserved) {
                $rawOutcome = 'SETTINGS_APPLICATION_RETURNED_NO_SURFACE_EVIDENCE'
                $earliest = 'settings-surface'
            } else {
                $rawOutcome = 'SETTINGS_LAUNCH_SUCCEEDED'
                $earliest = $null
            }
        }
    } elseif ($malformed -gt 0) {
        $rawOutcome = 'INVALID_MARKERS'
        $earliest = $null
        $orderingValid = $false
    }

    $boundaryInferences = @(
        'MISSING_SETTINGS_LIFETIME_MARKER', 'FACTORY_ENTERED_NO_RETURN',
        'FACTORY_SUCCEEDED_REGISTRATION_INCOMPLETE',
        'REGISTRATION_SUCCEEDED_FOLLOWUP_INCOMPLETE',
        'TRY_START_RETURNED_CALLER_NOT_CONTINUED',
        'TRY_START_EXIT_OBSERVED_CALLER_RESULT_UNOBSERVED',
        'SETTINGS_APPLICATION_ENTRY_NO_RETURN', 'SETTINGS_DISPATCH_RETURN_UNOBSERVED',
        'SETTINGS_APPLICATION_RETURNED_NO_SURFACE_EVIDENCE', 'INVALID_MARKERS'
    )
    $outcome = $rawOutcome
    if ($CaptureState -eq 'incomplete' -and $rawOutcome -in $boundaryInferences) {
        $outcome = 'INSUFFICIENT_CAPTURE'
    } elseif ($CaptureState -eq 'in-progress' -and $rawOutcome -in $boundaryInferences) {
        $outcome = 'OBSERVATION_IN_PROGRESS'
    }

    $factoryStatus = 'not-entered'
    if ($stage.ContainsKey('factory-throw')) {
        $factoryStatus = 'threw'
    } elseif ($stage.ContainsKey('factory-return')) {
        $factoryStatus = if ($stage['factory-return'].code -eq 0 -and $stage['factory-return'].state -ne 'null') { 'returned-successfully' } else { 'returned-error' }
    } elseif ($stage.ContainsKey('factory-enter')) {
        $factoryStatus = switch ($CaptureState) {
            'complete' { 'entered-no-return-observed' }
            'in-progress' { 'entered-awaiting-return' }
            default { 'entered-capture-incomplete' }
        }
    }

    $registrationStatus = 'not-entered'
    if ($stage.ContainsKey('resident-register-return')) {
        $registrationStatus = if ($stage['resident-register-return'].code -eq 0) { 'returned-successfully' } else { 'returned-error' }
    } elseif ($stage.ContainsKey('resident-register-enter')) {
        $registrationStatus = switch ($CaptureState) {
            'complete' { 'entered-no-return-observed' }
            'in-progress' { 'entered-awaiting-return' }
            default { 'entered-capture-incomplete' }
        }
    }

    $lifetimeStatus = 'not-entered'
    if ($stage.ContainsKey('exit-success')) {
        $lifetimeStatus = 'returned-successfully'
    } elseif ($stage.ContainsKey('exit-failure')) {
        $lifetimeStatus = 'returned-failure'
    } elseif ($stage.ContainsKey('enter')) {
        $lifetimeStatus = switch ($CaptureState) {
            'complete' { 'entered-no-result-observed' }
            'in-progress' { 'entered-awaiting-result' }
            default { 'entered-capture-incomplete' }
        }
    }

    $lastObservedStage = if ($selected.Count -gt 0) { [string]$selected[-1].stage } else { $null }
    $lastCompletedStage = $null
    $completedStages = @('existing-resident','capacity-check','generation-check',
        'factory-resolve','factory-return','factory-throw','teardown-return',
        'teardown-throw','resident-register-return','owner-assign','exit-success',
        'exit-failure')
    foreach ($event in $selected) {
        if ($event.stage -in $completedStages) { $lastCompletedStage = [string]$event.stage }
    }
    $endedInInterval = $CaptureState -eq 'complete' -and
        $rawOutcome -in @('FACTORY_ENTERED_NO_RETURN',
            'FACTORY_SUCCEEDED_REGISTRATION_INCOMPLETE',
            'REGISTRATION_SUCCEEDED_FOLLOWUP_INCOMPLETE',
            'TRY_START_RETURNED_CALLER_NOT_CONTINUED',
            'TRY_START_EXIT_OBSERVED_CALLER_RESULT_UNOBSERVED',
            'SETTINGS_APPLICATION_ENTRY_NO_RETURN',
            'SETTINGS_DISPATCH_RETURN_UNOBSERVED',
            'SETTINGS_APPLICATION_RETURNED_NO_SURFACE_EVIDENCE')

    return [pscustomobject]@{
        outcome=$outcome
        observedBoundaryOutcome=$rawOutcome
        earliestIncompleteStage=$earliest
        events=$selected
        malformedMarkerCount=$malformed
        markerOrderingValid=$orderingValid
        invocation=$invocation
        settingsSurfaceObserved=[bool]$surfaceObserved
        callerContinued=$callerContinued
        captureState=$CaptureState
        captureReason=if ($CaptureState -eq 'complete') { 'serial-capture-verified-complete' } elseif ($malformed -gt 0) { 'partial-or-malformed-marker' } else { 'serial-capture-not-proven-complete' }
        factoryStatus=$factoryStatus
        registrationStatus=$registrationStatus
        lifetimeStatus=$lifetimeStatus
        lastObservedStage=$lastObservedStage
        lastCompletedStage=$lastCompletedStage
        traceEndedWithinInstrumentedInterval=[bool]$endedInInterval
    }
}
