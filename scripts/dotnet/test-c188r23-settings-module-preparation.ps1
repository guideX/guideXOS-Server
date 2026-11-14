$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$runtimePlatform = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_platform.cpp') -Raw
$runtimeBuilder = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\build-runtime-pack.ps1') -Raw
$managedBuilder = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\build-managed-hostlog-proof.ps1') -Raw
$kernelMarkers = Get-Content -LiteralPath (Join-Path $repo 'kernel\core\nativeaot_application.cpp') -Raw
$candidateBuilder = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\build-c188r23-settings-candidate.ps1') -Raw
$startupShim = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\c102_startup.c') -Raw
. (Join-Path $PSScriptRoot 'c188r23-preparation-boundary-predicates.ps1')

$cases = 0
function Assert-R23([bool]$Condition, [string]$Name) {
    $script:cases++
    if (-not $Condition) { throw "R23 Settings module preparation regression failed: $Name" }
}

$registrationPosition = $runtimePlatform.IndexOf('guidexos_nativeaot_pal_startup_marker(0xC188B006u)')
$registerCallPosition = $runtimePlatform.IndexOf('if (!RhRegisterOSModule(')
$initializeMarkerPosition = $runtimePlatform.IndexOf('guidexos_nativeaot_pal_startup_marker(0xC188B007u)')
$initializeCallPosition = $runtimePlatform.IndexOf('    InitializeModules(')
Assert-R23 ($registrationPosition -ge 0 -and $registrationPosition -lt $registerCallPosition) 'B006 marks entry to actual RhRegisterOSModule call'
Assert-R23 ($initializeMarkerPosition -gt $registerCallPosition -and $initializeMarkerPosition -lt $initializeCallPosition) 'B007 follows successful code-manager registration and immediately precedes generated InitializeModules'
Assert-R23 ($runtimePlatform.Contains('GUIDEXOS_NATIVEAOT_C188R23_SETTINGS_PREPARATION_TRACE') -and
    $runtimePlatform.Contains('guidexos_nativeaot_pal_startup_marker')) 'checkpoints use the established PAL startup callback and are profile gated'
Assert-R23 ($kernelMarkers.Contains('code-manager-registration-begin') -and
    $kernelMarkers.Contains('initialize-modules-call')) 'kernel records human-readable meanings for both preparation checkpoints'
Assert-R23 ($runtimeBuilder.Contains('[switch]$C188R23SettingsPreparationTrace') -and
    $runtimeBuilder.Contains('/DGUIDEXOS_NATIVEAOT_C188R23_SETTINGS_PREPARATION_TRACE') -and
    $runtimeBuilder.Contains('c188r23SettingsPreparationTrace = [bool]$C188R23SettingsPreparationTrace')) 'runtime-pack profile is distinct and records its trace flag'
Assert-R23 ($managedBuilder.Contains('[switch]$C188R23SettingsPreparationTrace') -and
    $managedBuilder.Contains('R23 preparation trace requires the established R22 handoff') -and
    $managedBuilder.Contains('"-C188R23SettingsPreparationTrace"')) 'managed builder forwards R23 and requires the real R21/R22 profile'
Assert-R23 ($startupShim.Contains('guidexos_nativeaot_prepare_managed_entry()') -and
    $startupShim.Contains('context->startupMarker(C188R11_STARTUP_BOOTSTRAP_READY)')) 'R23 retains the real shared preparation and existing return boundary'
Assert-R23 ($candidateBuilder.Contains('ManagedSettingsCenter___ctor_0') -and
    $candidateBuilder.Contains('guidexos_nativeaot_prepare_managed_entry') -and
    $candidateBuilder.Contains('candidate-integrity.json') -and
    $candidateBuilder.Contains('C188R23_SETTINGS_MODULE_PREPARATION')) 'candidate gate keeps the real Settings factory and complete integrity inventory'

$registrationOnly = Get-C188R23PreparationBoundaryEvidence @'
[C102-STARTUP] stage=C188B004
'@
Assert-R23 ($registrationOnly.prepareBeginObserved -and
    -not $registrationOnly.codeManagerRegistrationCallSiteReached -and
    $registrationOnly.firstMissingMilestone -eq 'module-discovery-or-validation-before-RhRegisterOSModule-call-site') 'entry-only trace localizes the pre-registration interval'
$registered = Get-C188R23PreparationBoundaryEvidence @'
[C102-STARTUP] stage=C188B004
[C102-STARTUP] stage=C188B006
'@
Assert-R23 ($registered.codeManagerRegistrationCallSiteReached -and
    -not $registered.initializeModulesCallSiteReached -and
    $registered.firstMissingMilestone -eq 'RhRegisterOSModule-entry-or-return-after-call-site') 'registration call-site without the following checkpoint leaves RhRegisterOSModule entry/return unresolved'
$moduleCallSite = Get-C188R23PreparationBoundaryEvidence @'
[C102-STARTUP] stage=C188B004
[C102-STARTUP] stage=C188B006
[C102-STARTUP] stage=C188B007
'@
Assert-R23 ($moduleCallSite.initializeModulesCallSiteReached -and
    $moduleCallSite.codeManagerRegistrationReturned -and
    -not $moduleCallSite.prepareReturned -and
    $moduleCallSite.firstMissingMilestone -eq 'InitializeModules-call-site-to-shared-preparation-return' -and
    -not $moduleCallSite.initializeModulesCalleeEntryObserved) 'pre-call checkpoint and absent helper return do not claim NativeAOT callee entry'
$preparedSettingsPending = Get-C188R23PreparationBoundaryEvidence @'
[C102-STARTUP] stage=C188B004
[C102-STARTUP] stage=C188B006
[C102-STARTUP] stage=C188B007
[C102-STARTUP] stage=C188B005
[C102-STARTUP] stage=C188B002
'@
Assert-R23 ($preparedSettingsPending.prepareReturned -and
    $preparedSettingsPending.c188b002Observed -and
    -not $preparedSettingsPending.realSettingsFactoryEntered -and
    $preparedSettingsPending.firstMissingMilestone -eq 'managed-Settings-launch-entry' -and
    -not $preparedSettingsPending.factoryExecutionVerified) 'preparation return never fabricates Settings factory execution'

if ($cases -ne 12) { throw "R23 regression case count mismatch: $cases" }
Write-Output "C188R23-SETTINGS-MODULE-PREPARATION cases=$cases result=PASS"
