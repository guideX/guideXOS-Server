function Get-C188R23PreparationBoundaryEvidence([string]$SerialText) {
    $prepareBegin = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B004\s*\r?$')
    $registrationCallSite = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B006\s*\r?$')
    $initializeModulesCallSite = [regex]::IsMatch($SerialText,
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
    } elseif (-not $registrationCallSite) {
        'module-discovery-or-validation-before-RhRegisterOSModule-call-site'
    } elseif (-not $initializeModulesCallSite) {
        'RhRegisterOSModule-entry-or-return-after-call-site'
    } elseif (-not $prepareReturned) {
        'InitializeModules-call-site-to-shared-preparation-return'
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
        codeManagerRegistrationCallSiteReached=$registrationCallSite
        initializeModulesCallSiteReached=$initializeModulesCallSite
        codeManagerRegistrationReturned=$initializeModulesCallSite
        prepareReturned=$prepareReturned
        initializeModulesCalleeEntryObserved=$false
        c188b002Observed=$nativeReady
        managedSettingsLaunchEntered=$settingsLaunch
        realSettingsFactoryEntered=$settingsFactory
        firstMissingMilestone=$firstMissing
        factoryExecutionVerified=$false
    }
}
