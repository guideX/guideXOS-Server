function Get-C188R22RunnerTerminalEvidence(
    [string]$RunnerStdout,
    [string]$RunnerStderr,
    [Nullable[int]]$RunnerExitCode,
    [string]$QmpControlText = '') {
    $runnerText = [string]$RunnerStdout + "`n" + [string]$RunnerStderr
    $readinessTimeout = $runnerText -match 'Guest did not reach the input loop before timeout\.'
    $guestExitedBeforeReady = $runnerText -match 'QAC/QEMU exited before guest readiness:'
    $qmpStartTimeout = $runnerText -match 'QMP did not become available before the guest boot timeout\.'
    $admissionTimeout = $runnerText -match 'QAC did not grant exclusive acceptance admission before timeout\.'
    $runnerQuitAfterTimeout = $readinessTimeout -and
        ([string]$QmpControlText -match '"execute"\s*:\s*"quit"|QMP.*\bquit\b')

    $terminalCause = if ($readinessTimeout) { 'guest-input-loop-readiness-timeout' }
        elseif ($guestExitedBeforeReady) { 'qac-qemu-exited-before-guest-readiness' }
        elseif ($qmpStartTimeout) { 'qmp-start-timeout' }
        elseif ($admissionTimeout) { 'qac-admission-timeout' }
        elseif ($null -eq $RunnerExitCode) { 'runner-still-active-or-exit-unobserved' }
        elseif ($RunnerExitCode -ne 0) { 'runner-failed-unclassified' }
        else { 'runner-completed-without-readiness-failure' }

    return [ordered]@{
        terminalCause=$terminalCause
        runnerExitCode=$RunnerExitCode
        readinessTimeout=[bool]$readinessTimeout
        guestExitedBeforeReadiness=[bool]$guestExitedBeforeReady
        qmpStartTimeout=[bool]$qmpStartTimeout
        qacAdmissionTimeout=[bool]$admissionTimeout
        runnerQuitAfterReadinessTimeout=[bool]$runnerQuitAfterTimeout
        qmpQuitObserved=([string]$QmpControlText -match '"execute"\s*:\s*"quit"|QMP.*\bquit\b')
    }
}

function Get-C188R22BootstrapBoundaryEvidence([string]$SerialText) {
    $stage6 = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=00000006\s*\r?$')
    $stage7 = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=00000007\s*\r?$')
    $prepareBegin = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B004\s*\r?$')
    $prepareReady = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B005\s*\r?$')
    $nativeReady = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B002\s*\r?$')
    $managedSettingsLaunch = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=launch-entry selector=5\b')
    $factoryEntered = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R21-(?:STACK stage=settings-factory-entry|FACTORY invocation=)')
    $firstMissing = if (-not $stage6) { 'RhInitialize-returned' }
        elseif (-not $stage7) { 'current-thread-TLS-installed' }
        elseif (-not $prepareBegin) { 'shared-NativeAOT-entry-preparation-began' }
        elseif (-not $prepareReady) { 'InitializeModules-and-code-manager-preparation-returned' }
        elseif (-not $nativeReady) { 'C102-startup-ready-state-published' }
        elseif (-not $managedSettingsLaunch) { 'managed-Settings-launch-entry' }
        elseif (-not $factoryEntered) { 'real-selector-5-factory-entry' }
        else { 'none-before-factory-entry' }
    return [ordered]@{
        nativeRuntimeInitialized=($stage6 -and $stage7)
        prepareBeginObserved=$prepareBegin
        prepareReturned=$prepareReady
        c188b002Observed=$nativeReady
        managedSettingsLaunchEntered=$managedSettingsLaunch
        realSettingsFactoryEntered=$factoryEntered
        firstMissingMilestone=$firstMissing
        c188b002IsManagedEntry=$false
    }
}

function Get-C188R22MissingCandidateSymbols(
    [string]$MapText,
    [string[]]$RequiredSymbols) {
    return @($RequiredSymbols | Where-Object {
        -not ([string]$MapText).Contains([string]$_)
    })
}
