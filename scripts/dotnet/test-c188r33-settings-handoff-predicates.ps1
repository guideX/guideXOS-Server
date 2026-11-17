$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'c188r11-runner-predicates.ps1')
. (Join-Path $PSScriptRoot 'c188r11r4r2-runner-predicates.ps1')
. (Join-Path $PSScriptRoot 'c188r33-settings-handoff-predicates.ps1')

$passed = 0
$fixturePath = Join-Path $PSScriptRoot 'fixtures/c188r33/r32-post-initialization.serial.txt'
$r32Serial = Get-Content -LiteralPath $fixturePath -Raw
$r32Milestones = Test-C188R33R32InitializerDoesNotProveSettingsLaunch $r32Serial

if (-not $r32Milestones.initializeModulesReturned -or -not $r32Milestones.startupReady -or
    -not $r32Milestones.inputLoopReached) {
    throw 'R32 completed initialization, startup readiness, or input-loop evidence was missed.'
}
$passed++
if ($r32Milestones.selector5LaunchObserved -or $r32Milestones.genuineFactoryEntryObserved -or
    $r32Milestones.moduleInitializationImpliesSettingsFactoryEntry) {
    throw 'R32 initialization markers were incorrectly treated as selector-5 dispatch or factory entry.'
}
$passed++
if (-not $r32Milestones.missingSettingsLaunch -or
    -not $r32Milestones.missingLaunchIsNotManagedCrash -or
    $r32Milestones.fatalFailureMarkerObserved) {
    throw 'A missing Settings launch after the input loop was misclassified as a managed crash.'
}
$passed++

$r30Profile = 'C188R30_THREAD_ATTACHMENT_TRACE_TWO_DESCRIPTORS'
if (-not (Test-C188R33SettingsClickGate $r32Serial $r30Profile 2 1280 800)) {
    throw 'R32 real Notes readiness and surface state did not open the verified Settings-click gate.'
}
$passed++
$legacyReadinessFailure = Get-C188R11ReadinessFailureReason $r32Serial
if ($legacyReadinessFailure -cne 'managed-return-state-missing-or-invalid') {
    throw 'The legacy five-descriptor readiness audit no longer rejects the R30 two-descriptor profile.'
}
$passed++
if (-not [string]::IsNullOrEmpty((Get-C188R11ReadinessFailureReason $r32Serial 2))) {
    throw 'The profile-bound post-readiness audit rejected the valid R30 two-descriptor state.'
}
$passed++
$settingsRunnerPath = Join-Path $PSScriptRoot 'run-c188r12r1-settings-handoff.ps1'
$settingsRunnerSource = Get-Content -LiteralPath $settingsRunnerPath -Raw
if ($settingsRunnerSource -notmatch 'Get-C188R11ReadinessFailureReason\s+\$serialText\s+\x60?\s*\(\[uint32\]\$readinessProfile\.expectedDescriptorCount\)') {
    throw 'The nested Settings runner post-readiness audit is not bound to its candidate readiness profile.'
}
$passed++
if (Test-C188R33SettingsClickGate $r32Serial $r30Profile 5 1280 800) {
    throw 'R30 two-descriptor managed return was accepted under the five-descriptor profile.'
}
$passed++
if (Test-C188R33SettingsClickGate $r32Serial $r30Profile 2 0 800) {
    throw 'Missing QAC-provided screen dimensions were accepted for the Settings-click gate.'
}
$passed++
if (Test-C188R33SettingsClickGate '[C102-STARTUP] stage=C188B002' $r30Profile 2 1280 800) {
    throw 'A startup readiness marker substituted for real Notes/App Model state.'
}
$passed++

$missingOrdinaryReady = $r32Serial -replace '(?m)^\[C102-MANAGED-OUTPUT\] C166-ORDINARY-READY.*\r?\n', ''
if (Test-C188R33SettingsClickGate $missingOrdinaryReady $r30Profile 2 1280 800) {
    throw 'C188B002 and a valid managed return substituted for the R30 application render-ready marker.'
}
$passed++
$missingSurfaceFocus = $r32Serial -replace '(?m)^\[C188R12R1-SURFACE\] stage=created.*\r?\n', ''
if (Test-C188R33SettingsClickGate $missingSurfaceFocus $r30Profile 2 1280 800) {
    throw 'A Notes launch and render marker substituted for native focused/front surface state.'
}
$passed++
$alreadyLaunchedSettings = $r32Serial + "`n[APPMODEL-MANAGED-LAUNCH] source=DesktopLaunch appId=com.guidexos.apps.managed.settingscenter selector=00000005 image=/system/apps/GXOSAPP.ELF metadata=valid`n"
if (Test-C188R33SettingsClickGate $alreadyLaunchedSettings $r30Profile 2 1280 800) {
    throw 'The runner allowed a duplicate Settings click after a real App Model launch request.'
}
$passed++
$alreadyEnteredFactory = $r32Serial + "`n[C102-MANAGED-OUTPUT] C188R12R4_FACTORY invocation=1 selector=5 operation=0 stage=factory-invoke code=0 result=begin`n"
if (Test-C188R33SettingsClickGate $alreadyEnteredFactory $r30Profile 2 1280 800) {
    throw 'The runner allowed a duplicate click after genuine Settings factory entry.'
}
$passed++

$tempBase = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
$tempRoot = [System.IO.Path]::GetFullPath((Join-Path $tempBase ('c188r33-readiness-' + [guid]::NewGuid().ToString('N'))))
if (-not $tempRoot.StartsWith($tempBase.TrimEnd('\') + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Readiness-profile test path escaped the system temporary directory.'
}
$null = New-Item -ItemType Directory -Path $tempRoot
try {
    $runtimeManifestPath = Join-Path $tempRoot 'runtime-pack.manifest.json'
    $runtimeManifest = [ordered]@{
        identity='guidexos-nativeaot-runtime-pack-amd64-c188r30-thread-attachment-trace-v1'
        c188r30ThreadAttachmentTrace=$true
        c188r30ThreadAttachmentTraceMarker='GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE'
        runtimeIdentity=[ordered]@{gc='Workstation'}
        imageBackedArenaPresent=$false
    }
    $runtimeManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimeManifestPath -Encoding utf8
    $runtimeHash = (Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash
    $candidateManifestPath = Join-Path $tempRoot 'candidate-manifest.json'
    $candidateManifest = [ordered]@{
        runtimePackManifest=$runtimeManifestPath
        runtimePackManifestSha256=$runtimeHash
        actualManagedSettingsFactory='GuideXosApplicationRegistry selector 5 -> ManagedSettingsCenter'
        actualFactoryConstructorSymbol='ManagedSettingsCenter___ctor_0'
    }
    $candidateManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidateManifestPath -Encoding utf8

    $profile = Get-C188R33ManagedReadinessProfile $tempRoot
    if ([string]$profile.profile -cne $r30Profile -or
        [uint32]$profile.expectedDescriptorCount -ne 2 -or
        [string]$profile.runtimePackManifestSha256 -ine $runtimeHash) {
        throw 'Candidate/runtime manifests did not establish the exact R30 readiness profile.'
    }
    $passed++

    $candidateManifest.runtimePackManifestSha256=$null
    $candidateManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidateManifestPath -Encoding utf8
    $missingRuntimeHashRejected = $false
    try { $null = Get-C188R33ManagedReadinessProfile $tempRoot }
    catch { $missingRuntimeHashRejected = $true }
    if (-not $missingRuntimeHashRejected) {
        throw 'An R30 two-descriptor profile without a candidate-bound runtime manifest hash was accepted.'
    }
    $passed++
    $candidateManifest.runtimePackManifestSha256=$runtimeHash
    $candidateManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidateManifestPath -Encoding utf8

    $runtimeManifest.c188r30ThreadAttachmentTraceMarker='WRONG_PROFILE_MARKER'
    $runtimeManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimeManifestPath -Encoding utf8
    $candidateManifest.runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash
    $candidateManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidateManifestPath -Encoding utf8
    $badMarkerRejected = $false
    try { $null = Get-C188R33ManagedReadinessProfile $tempRoot }
    catch { $badMarkerRejected = $true }
    if (-not $badMarkerRejected) { throw 'A mismatched R30 runtime marker was accepted.' }
    $passed++

    $runtimeManifest.c188r30ThreadAttachmentTraceMarker='GUIDEXOS_NATIVEAOT_C188R30_THREAD_ATTACHMENT_TRACE'
    $runtimeManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $runtimeManifestPath -Encoding utf8
    $candidateManifest.runtimePackManifestSha256=(Get-FileHash -LiteralPath $runtimeManifestPath -Algorithm SHA256).Hash
    $candidateManifest.actualManagedSettingsFactory='GuideXosApplicationRegistry selector 5 -> SyntheticSettingsFactory'
    $candidateManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidateManifestPath -Encoding utf8
    $badFactoryRejected = $false
    try { $null = Get-C188R33ManagedReadinessProfile $tempRoot }
    catch { $badFactoryRejected = $true }
    if (-not $badFactoryRejected) { throw 'A synthetic selector-5 factory passed R30 readiness-profile validation.' }
    $passed++

    $legacyRoot = Join-Path $tempRoot 'legacy-prelaunch-fixture'
    $null = New-Item -ItemType Directory -Path $legacyRoot
    Set-Content -LiteralPath (Join-Path $legacyRoot 'candidate-manifest.json') `
        -Value '{"candidate":"legacy-prelaunch-fixture"}' -Encoding utf8
    $legacyProfile = Get-C188R33ManagedReadinessProfile $legacyRoot
    if ([string]$legacyProfile.profile -cne 'C188R11_FIVE_DESCRIPTOR_COMPATIBILITY' -or
        [uint32]$legacyProfile.expectedDescriptorCount -ne 5) {
        throw 'The minimal prelaunch fixture lost its legacy compatibility readiness profile.'
    }
    $passed++
} finally {
    $resolvedTempRoot = [System.IO.Path]::GetFullPath($tempRoot)
    if ($resolvedTempRoot.StartsWith($tempBase.TrimEnd('\') + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTempRoot -PathType Container)) {
        Remove-Item -LiteralPath $resolvedTempRoot -Recurse -Force
    }
}

Write-Output "C188R33_SETTINGS_HANDOFF_TESTS=$passed RESULT=PASS"
