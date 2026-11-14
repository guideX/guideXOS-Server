$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$registry = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\GuideXos\GuideXosApplication.cs') -Raw
$project = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\HostLogProof.csproj') -Raw
$factory = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\GuideXos\C188R12R4FactoryDiagnostics.cs') -Raw
$settings = Get-Content -LiteralPath (Join-Path $repo 'samples\managed\HostLogProof\Applications\ManagedSettingsCenter.cs') -Raw
$runtimeBuilder = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\build-runtime-pack.ps1') -Raw
$managedBuilder = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\build-managed-hostlog-proof.ps1') -Raw
$unwindSource = Get-Content -LiteralPath (Join-Path $repo 'tools\dotnet\runtime-pack\src\platform\guidexos_nativeaot_startup_imports.cpp') -Raw
$unwindRegression = Get-Content -LiteralPath (Join-Path $repo 'scripts\dotnet\test-c188r20-iterator-advancement.py') -Raw
$cases = 0
function Assert-R21([bool]$Condition, [string]$Name) {
    $script:cases++
    if (-not $Condition) { throw "R21 Settings factory regression failed: $Name" }
}

Assert-R21 ($registry -match '(?s)RegisterC188R11Descriptor\(5u, "Managed Settings Center"u8,\s*#if HOSTLOGPROOF_C188R12R4_FACTORY\s*static \(\) => C188R12R4FactoryDiagnostics\.CreateSettingsCenter\(\)') 'selector 5 resolves to the actual Settings factory'
$ownershipProofCondition = @'
Condition="'$(HostLogProofC188R12R4R5GcOwnershipProof)' == 'true' and '$(HostLogProofC188R21ManagedSettingsFactory)' != 'true'"
'@
Assert-R21 ($project.Contains($ownershipProofCondition)) 'R21 keeps allocator authorization separate from the standalone GC proof entry point'
Assert-R21 ($settings.Contains('public sealed class ManagedSettingsCenter') -and
    $settings.Contains('C188R12R4FactoryDiagnostics.Emit("constructor-enter"u8') -and
    $settings.Contains('C188R12R4FactoryDiagnostics.Emit("controls-init-return"u8')) 'existing Settings constructor and control initialization remain in the path'
Assert-R21 ($factory.Contains('NativeGetMaximumStackBounds') -and
    $factory.Contains('currentStackPointer - stackLow') -and
    $factory.Contains('C188R21-STACK stage=settings-factory-entry')) 'factory-entry stack budget comes from live PAL bounds and a stack marker'
Assert-R21 ($registry.Contains('new WeakReference<GuideXosApplication>(application)') -and
    $registry.Contains('application = null;') -and
    $registry.Contains('GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced') -and
    $registry.Contains('s_lifetime.TryGet(selector, generation') -and
    $registry.Contains('ReferenceEquals(weakTarget, resident)')) 'temporary weak reference proves the existing App Model root across a real collection'
Assert-R21 ($managedBuilder.Contains('C188R21ManagedSettingsFactory') -and
    $managedBuilder.Contains('$managedProjectMode -ne ''C160Composite''') -and
    $runtimeBuilder.Contains('experimental-nativeaot-workstation-gc-object-and-array-allocation-no-c188-arena')) 'R21 profile binds C160 application ownership to real Workstation GC without the arena'
Assert-R21 ($unwindSource -match '(?s)guidexos_nativeaot_startup_RtlVirtualUnwind\s*\(.*?\)\s*\{.*?guideXosRtlVirtualUnwindWindows\(' -and
    $unwindRegression.Contains('xor eax,eax; ret') -and
    $unwindRegression.Contains('guideXosRtlVirtualUnwindWindows(')) 'R20 startup import regression rejects the known no-op unwind path'

. (Join-Path $PSScriptRoot 'c188r12r1-settings-handoff-predicates.ps1')
$validR21Capture = @'
[C102-MANAGED-OUTPUT] C188R12R3_LIFETIME invocation=17 stage=exit-success selector=5 identity=com.guidexos.apps.managed.settingscenter count=1 mask=1 generation=4 code=0 state=resident
[C102-MANAGED-OUTPUT] C188R21-ROOT stage=post-gc selector=5 generation=4 gc=1 weak=1 owner=1 same=1 result=PASS
[C102-MANAGED-OUTPUT] C188R11 state seq=8 st=settings-launch-return selector=5 reset=0 m=1 g=4 td=0 n=5 result=0 gc=1 w=0:0
'@
$rootEvidence = Get-C188R12R1SettingsHandoffEvidence $validR21Capture
Assert-R21 ($rootEvidence.r21ManagedRootVerified -and
    $rootEvidence.settingsRegistryReturnValid -and
    $rootEvidence.settingsRegistryMemoryStable -and
    $rootEvidence.persistentHeapPreserved) 'R21 compacting-GC root evidence satisfies the settings handoff ownership predicate'
$invalidR21Capture = $validR21Capture.Replace('generation=4 gc=1 weak=1 owner=1 same=1 result=PASS',
    'generation=3 gc=1 weak=1 owner=1 same=1 result=PASS')
$invalidEvidence = Get-C188R12R1SettingsHandoffEvidence $invalidR21Capture
Assert-R21 (-not $invalidEvidence.r21ManagedRootVerified -and
    -not $invalidEvidence.settingsRegistryReturnValid) 'generation mismatch fails closed in the R21 root predicate'

if ($cases -ne 9) { throw "R21 regression case count mismatch: $cases" }
Write-Output "C188R21-SETTINGS-FACTORY cases=$cases result=PASS"
