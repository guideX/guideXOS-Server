function Get-C188R23PreparationBoundaryEvidence([string]$SerialText) {
    $prepareBegin = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B004\s*\r?$')
    $registrationBegin = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B006\s*\r?$')
    $initializeModulesCall = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B007\s*\r?$')
    $prepareReturned = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B005\s*\r?$')
    $nativeReady = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B002\s*\r?$')
    $settingsLaunch = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=launch-entry selector=5\b')
    $settingsFactory = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R21-(?:STACK stage=settings-factory-entry|FACTORY invocation=)')

    $firstMissing = if (-not $prepareBegin) {
        'preparation-entry-not-observed'
    } elseif (-not $registrationBegin) {
        'module-discovery-or-validation-before-RhRegisterOSModule'
    } elseif (-not $initializeModulesCall) {
        'RhRegisterOSModule-completion-before-InitializeModules'
    } elseif (-not $prepareReturned) {
        'InitializeModules-returned-to-shared-preparation'
    } elseif (-not $nativeReady) {
        'C102-startup-ready-state-published'
    } elseif (-not $settingsLaunch) {
        'managed-Settings-launch-entry'
    } elseif (-not $settingsFactory) {
        'real-selector-5-factory-entry'
    } else {
        'none-before-factory-entry'
    }

    return [ordered]@{
        prepareBeginObserved=$prepareBegin
        codeManagerRegistrationCallEntered=$registrationBegin
        initializeModulesCallIssued=$initializeModulesCall
        prepareReturned=$prepareReturned
        c188b002Observed=$nativeReady
        managedSettingsLaunchEntered=$settingsLaunch
        realSettingsFactoryEntered=$settingsFactory
        firstMissingMilestone=$firstMissing
        factoryExecutionVerified=$false
    }
}
