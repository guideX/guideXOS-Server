function Get-C188R33ManifestValue(
    [object]$Manifest,
    [string]$PropertyName) {
    $property = $Manifest.PSObject.Properties[$PropertyName]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Get-C188R33ManagedReadinessProfile(
    [string]$CandidateRoot) {
    $candidateManifestPath = Join-Path $CandidateRoot 'candidate-manifest.json'
    if (-not (Test-Path -LiteralPath $candidateManifestPath -PathType Leaf)) {
        return [ordered]@{
            profile='C188R11_FIVE_DESCRIPTOR_COMPATIBILITY'
            expectedDescriptorCount=[uint32]5
            runtimePackManifestPath=''
            runtimePackManifestSha256=''
        }
    }

    $candidateManifest = Get-Content -LiteralPath $candidateManifestPath -Raw |
        ConvertFrom-Json -ErrorAction Stop
    $runtimePackManifestPath = [string](Get-C188R33ManifestValue $candidateManifest 'runtimePackManifest')
    if ([string]::IsNullOrWhiteSpace($runtimePackManifestPath)) {
        $buildProfile = [string](Get-C188R33ManifestValue $candidateManifest 'buildProfile')
        $runtimeProfile = [string](Get-C188R33ManifestValue $candidateManifest 'runtimeProfile')
        if ($buildProfile -match 'R30ThreadAttachmentTrace' -or
            $runtimeProfile -match 'c188r30-thread-attachment-trace') {
            throw 'Candidate claims an R30 readiness profile without its runtime-pack manifest.'
        }
        return [ordered]@{
            profile='C188R11_FIVE_DESCRIPTOR_COMPATIBILITY'
            expectedDescriptorCount=[uint32]5
            runtimePackManifestPath=''
            runtimePackManifestSha256=''
        }
    }
    if (-not (Test-Path -LiteralPath $runtimePackManifestPath -PathType Leaf)) {
        throw 'Candidate runtime-pack manifest is missing; managed readiness profile cannot be established.'
    }
    $runtimePackManifestHash = (Get-FileHash -LiteralPath $runtimePackManifestPath -Algorithm SHA256).Hash
    $claimedRuntimeManifestHash = [string](Get-C188R33ManifestValue $candidateManifest 'runtimePackManifestSha256')
    if (-not [string]::IsNullOrWhiteSpace($claimedRuntimeManifestHash) -and
        $runtimePackManifestHash -ine $claimedRuntimeManifestHash) {
        throw 'Candidate runtime-pack manifest hash does not match the candidate manifest.'
    }
    $runtimeManifest = Get-Content -LiteralPath $runtimePackManifestPath -Raw |
        ConvertFrom-Json -ErrorAction Stop
    $r30Identity = 'guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1'
    $runtimeIdentity = Get-C188R33ManifestValue $runtimeManifest 'runtimeIdentity'
    $isR30Identity = [string](Get-C188R33ManifestValue $runtimeManifest 'identity') -ceq $r30Identity
    $hasR30Marker = [bool](Get-C188R33ManifestValue $runtimeManifest 'c188r30ThreadAttachmentTrace')
    if ($isR30Identity -or $hasR30Marker) {
        $runtimeTraceMarker = [string](Get-C188R33ManifestValue $runtimeManifest 'c188r30ThreadAttachmentTraceMarker')
        $runtimeGc = [string](Get-C188R33ManifestValue $runtimeIdentity 'gc')
        $imageBackedArenaPresent = [bool](Get-C188R33ManifestValue $runtimeManifest 'imageBackedArenaPresent')
        $settingsFactory = [string](Get-C188R33ManifestValue $candidateManifest 'actualManagedSettingsFactory')
        $factoryConstructor = [string](Get-C188R33ManifestValue $candidateManifest 'actualFactoryConstructorSymbol')
        if ([string]::IsNullOrWhiteSpace($claimedRuntimeManifestHash) -or
            -not $isR30Identity -or -not $hasR30Marker -or
            $runtimeTraceMarker -cne 'GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE' -or
            $runtimeGc -cne 'Workstation' -or
            $imageBackedArenaPresent -or
            $settingsFactory -cne 'GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter' -or
            $factoryConstructor -cne 'ManagedSettingsCenter___ctor_0') {
            throw 'R30 readiness profile does not bind the accepted runtime trace and genuine selector-5 Settings factory.'
        }
        return [ordered]@{
            profile='C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS'
            expectedDescriptorCount=[uint32]2
            runtimePackManifestPath=$runtimePackManifestPath
            runtimePackManifestSha256=$runtimePackManifestHash
        }
    }

    return [ordered]@{
        profile='C188R11_FIVE_DESCRIPTOR_COMPATIBILITY'
        expectedDescriptorCount=[uint32]5
        runtimePackManifestPath=$runtimePackManifestPath
        runtimePackManifestSha256=$runtimePackManifestHash
    }
}

function Test-C188R33R32InitializerDoesNotProveSettingsLaunch(
    [string]$SerialText) {
    $initializeReturned = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B005\s*$')
    $startupReady = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-STARTUP\] stage=C188B002\s*$')
    $selector5Launch = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R11 state seq=\d+ st=launch-entry selector=5\b')
    $factoryEntered = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R4_FACTORY invocation=\d+ selector=5 operation=\d+ stage=factory-invoke code=0 result=begin\s*$')
    $inputLoopReached = [regex]::IsMatch($SerialText,
        '(?m)^\[KERNEL\] Entering main loop \(waiting for input\)\.\.\.\s*$')
    $fatalMarkerObserved = $SerialText.Contains('[C188R25-FAILFAST-CAPTURED]')
    $missingSettingsLaunch = $initializeReturned -and $startupReady -and
        $inputLoopReached -and -not $selector5Launch -and -not $factoryEntered
    return [ordered]@{
        initializeModulesReturned=$initializeReturned
        startupReady=$startupReady
        inputLoopReached=$inputLoopReached
        selector5LaunchObserved=$selector5Launch
        genuineFactoryEntryObserved=$factoryEntered
        fatalFailureMarkerObserved=$fatalMarkerObserved
        missingSettingsLaunch=$missingSettingsLaunch
        missingLaunchIsNotManagedCrash=($missingSettingsLaunch -and -not $fatalMarkerObserved)
        moduleInitializationImpliesSettingsFactoryEntry=($initializeReturned -and $factoryEntered)
    }
}

function Test-C188R33NotesApplicationReady(
    [string]$SerialText,
    [string]$ReadinessProfile) {
    if (-not (Test-C188R11NotesReady $SerialText)) { return $false }
    if ($ReadinessProfile -ceq 'C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS') {
        return [regex]::IsMatch($SerialText,
            '(?m)^\[C102-MANAGED-OUTPUT\] C166-ORDINARY-READY app=ManagedNotes render=PASS\r?$')
    }
    return $true
}

function Test-C188R33SettingsClickGate(
    [string]$SerialText,
    [string]$ReadinessProfile,
    [uint32]$ExpectedDescriptorCount,
    [int]$ScreenWidth,
    [int]$ScreenHeight) {
    if ($ScreenWidth -le 0 -or $ScreenHeight -le 0 -or
        -not [regex]::IsMatch($SerialText,
            '(?m)^\[C102-STARTUP\] stage=C188B005\s*$') -or
        -not [regex]::IsMatch($SerialText,
            '(?m)^\[C102-STARTUP\] stage=C188B002\s*$') -or
        -not (Test-C188R11InitializationEvidence $SerialText) -or
        -not (Test-C188R11ManagedLaunchState $SerialText $ExpectedDescriptorCount) -or
        -not (Test-C188R11NativeLaunchBoundary $SerialText) -or
        -not (Test-C188R33NotesApplicationReady $SerialText $ReadinessProfile)) {
        return $false
    }

    $settingsLaunchAlreadyRequested = [regex]::IsMatch($SerialText,
        '(?m)^\[APPMODEL-MANAGED-LAUNCH\] source=DesktopLaunch appId=com\.guidexos\.apps\.managed\.settingscenter\b')
    $settingsFactoryAlreadyEntered = [regex]::IsMatch($SerialText,
        '(?m)^\[C102-MANAGED-OUTPUT\] C188R12R4_FACTORY invocation=\d+ selector=5 operation=\d+ stage=factory-invoke code=0 result=begin\s*$')
    if ($settingsLaunchAlreadyRequested -or $settingsFactoryAlreadyEntered) { return $false }

    $focusEvidence = Get-C188R11R4R2NotesFocusEvidence $SerialText
    return [bool]$focusEvidence.notesWindowActive
}
